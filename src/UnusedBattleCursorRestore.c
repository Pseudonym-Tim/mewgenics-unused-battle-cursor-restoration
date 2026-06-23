#include "UnusedBattleCursorRestore.h"
#include "mewjector.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static MewjectorAPI g_mj;
static HMODULE g_hModule = NULL;

static fn_cursor_pip_ctor g_origGroundMouseCursorPipCtor = NULL;
static fn_cursor_pip_ctor g_origMouseCursorPip3DCtor = NULL;
static fn_display_object_set_property g_origDisplayObjectSetProperty = NULL;

static volatile PVOID g_trackedPips[MAX_TRACKED_PIPS];
static volatile LONG g_loggedTempHideSuppressed = 0;
static volatile LONG g_loggedGroundCtor = 0;
static volatile LONG g_loggedPip3DCtor = 0;
static volatile LONG g_loggedFirstUnlink = 0;
static volatile LONG g_installed = 0;

static HANDLE g_timerQueue = NULL;
static HANDLE g_forceHideTimer = NULL;
static volatile LONG g_timerStarted = 0;

static void Log(const char* fmt, ...)
{
#if ENABLE_DEBUG_LOGS
    char buffer[768];
    va_list ap;

    if (!g_mj.Log)
    {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    g_mj.Log(MOD_NAME, "%s", buffer);
#else
    (void)fmt;
#endif
}

static void AddTrackedPip(void* object)
{
    int i;

    if (!object)
    {
        return;
    }

    for (i = 0; i < MAX_TRACKED_PIPS; ++i)
    {
        if (InterlockedCompareExchangePointer((PVOID volatile*)&g_trackedPips[i], NULL, NULL) == object)
        {
            return;
        }
    }

    for (i = 0; i < MAX_TRACKED_PIPS; ++i)
    {
        if (InterlockedCompareExchangePointer((PVOID volatile*)&g_trackedPips[i], object, NULL) == NULL)
        {
            return;
        }
    }

    // Keep newest pips tracked even if battle reloads many times..
    InterlockedExchangePointer((PVOID volatile*)&g_trackedPips[0], object);
}

static int IsTrackedPip(void* object)
{
    int i;

    if (!object)
    {
        return 0;
    }

    for (i = 0; i < MAX_TRACKED_PIPS; ++i)
    {
        if (InterlockedCompareExchangePointer((PVOID volatile*)&g_trackedPips[i], NULL, NULL) == object)
        {
            return 1;
        }
    }

    return 0;
}

static void ForceHideDisplayObjectBits(void* object)
{
    uint8_t* bytes;

    if (!object)
    {
        return;
    }

    bytes = (uint8_t*)object;

    __try
    {
        // Keep the previous inferred visible-bit clear..
        bytes[DISPLAY_OBJECT_FLAGS_OFFSET] &= (uint8_t)~DISPLAY_OBJECT_VISIBLE_MASK;

        // Also mark the native movieclip/sprite-side state as non-renderable/clean...
        bytes[DISPLAY_OBJECT_BYTE0C_OFFSET] = 0;
        bytes[DISPLAY_OBJECT_BYTE0D_OFFSET] &= (uint8_t)0x8F;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // (Object was destroyed or is not a display object at this point, we can ignore safely)...
    }
}

static int RemovePointerFromPackedArray(void** arrayBase, uint32_t* countPtr, void* object)
{
    uint32_t count;
    uint32_t readIndex;
    uint32_t writeIndex;
    int removed;

    if (!arrayBase || !countPtr || !object)
    {
        return 0;
    }

    count = *countPtr;

    if (count > 4096U)
    {
        return 0;
    }

    removed = 0;
    writeIndex = 0;

    for (readIndex = 0; readIndex < count; ++readIndex)
    {
        void* entry = arrayBase[readIndex];

        if (entry == object)
        {
            removed = 1;
            continue;
        }

        if (writeIndex != readIndex)
        {
            arrayBase[writeIndex] = entry;
        }

        ++writeIndex;
    }

    if (removed)
    {
        uint32_t i;

        for (i = writeIndex; i < count; ++i)
        {
            arrayBase[i] = NULL;
        }

        *countPtr = writeIndex;
    }

    return removed;
}

static int UnlinkPipFromParentDisplayLists(void* object)
{
    uint8_t* child;
    uint8_t* parent;
    int removed;

    if (!object)
    {
        return 0;
    }

    removed = 0;
    child = (uint8_t*)object;

    __try
    {
        parent = *(uint8_t**)(child + DISPLAY_OBJECT_PARENT_OFFSET);

        if (!parent)
        {
            return 0;
        }

        removed |= RemovePointerFromPackedArray(*(void***)(parent + PARENT_CHILD_ARRAY_OFFSET), (uint32_t*)(parent + PARENT_CHILD_COUNT_OFFSET), object);

        removed |= RemovePointerFromPackedArray(*(void***)(parent + PARENT_RENDER_ARRAY_OFFSET), (uint32_t*)(parent + PARENT_RENDER_COUNT_OFFSET), object);

        if (removed)
        {
            // Leave the object allocated, but detach it from the renderer/child traversal..
            *(void**)(child + DISPLAY_OBJECT_PARENT_OFFSET) = NULL;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }

    if (removed && InterlockedCompareExchange(&g_loggedFirstUnlink, 1, 0) == 0)
    {
        Log("Unlinked a cursor pip from parent display/render arrays! object=%p", object);
    }

    return removed;
}

static void KillPipObject(void* object)
{
    if (!object)
    {
        return;
    }

    ForceHideDisplayObjectBits(object);
    UnlinkPipFromParentDisplayLists(object);
    ForceHideDisplayObjectBits(object);
}

static void KillKnownPips(void)
{
    int i;

    for (i = 0; i < MAX_TRACKED_PIPS; ++i)
    {
        void* object = InterlockedCompareExchangePointer((PVOID volatile*)&g_trackedPips[i], NULL, NULL);

        if (object)
        {
            KillPipObject(object);
        }
    }
}

static void __fastcall HookTempHideMouseCursor(void)
{
    // Battlefield hover calls this to temporarily hide the custom Mew cursor! Deliberately swallow..
    if (InterlockedCompareExchange(&g_loggedTempHideSuppressed, 1, 0) == 0)
    {
        Log("Suppressed glaiel::TempHideMouseCursor");
    }

    KillKnownPips();
}

static void __fastcall HookDisplayObjectSetProperty(void* object, void* propertyName, void* value)
{
    if (g_origDisplayObjectSetProperty)
    {
        g_origDisplayObjectSetProperty(object, propertyName, value);
    }

    if (IsTrackedPip(object))
    {
        KillPipObject(object);
    }
}

static void* __fastcall HookGroundMouseCursorPipCtor(void* a1, void* a2, void* a3)
{
    void* object = NULL;

    if (g_origGroundMouseCursorPipCtor)
    {
        object = g_origGroundMouseCursorPipCtor(a1, a2, a3);
    }

    if (object)
    {
        AddTrackedPip(object);
        KillPipObject(object);

        if (InterlockedCompareExchange(&g_loggedGroundCtor, 1, 0) == 0)
        {
            Log("GroundMouseCursorPip constructed at %p; detached from display/render arrays", object);
        }
    }

    return object;
}

static void* __fastcall HookMouseCursorPip3DCtor(void* a1, void* a2, void* a3)
{
    void* object = NULL;

    if (g_origMouseCursorPip3DCtor)
    {
        object = g_origMouseCursorPip3DCtor(a1, a2, a3);
    }

    if (object)
    {
        AddTrackedPip(object);
        KillPipObject(object);

        if (InterlockedCompareExchange(&g_loggedPip3DCtor, 1, 0) == 0)
        {
            Log("MouseCursorPip3D constructed at %p; detached from display/render arrays", object);
        }
    }

    return object;
}

static VOID CALLBACK ForceHideTimerProc(PVOID parameter, BOOLEAN timerOrWaitFired)
{
    (void)parameter;
    (void)timerOrWaitFired;

    KillKnownPips();
}

static void StartForceHideTimer(void)
{
    if (InterlockedCompareExchange(&g_timerStarted, 1, 0) != 0)
    {
        return;
    }

    g_timerQueue = CreateTimerQueue();

    if (!g_timerQueue)
    {
        InterlockedExchange(&g_timerStarted, 0);
        return;
    }

    if (!CreateTimerQueueTimer(&g_forceHideTimer, g_timerQueue, ForceHideTimerProc, NULL, FORCE_HIDE_POLL_INTERVAL_MS, FORCE_HIDE_POLL_INTERVAL_MS, WT_EXECUTEDEFAULT))
    {
        DeleteTimerQueue(g_timerQueue);
        g_timerQueue = NULL;
        g_forceHideTimer = NULL;
        InterlockedExchange(&g_timerStarted, 0);
        return;
    }

    Log("Force-hide/unlink timer started");
}

static void StopForceHideTimer(void)
{
    if (g_timerQueue)
    {
        DeleteTimerQueueEx(g_timerQueue, INVALID_HANDLE_VALUE);
        g_timerQueue = NULL;
        g_forceHideTimer = NULL;
    }

    InterlockedExchange(&g_timerStarted, 0);
}

static int InstallHooks(void)
{
    void* trampoline;

    if (!MJ_Resolve(&g_mj))
    {
        return 0;
    }

    if (InterlockedCompareExchange(&g_installed, 1, 0) != 0)
    {
        return 1;
    }

    trampoline = NULL;

    if (!g_mj.InstallHook(RVA_TEMP_HIDE_MOUSE_CURSOR, TEMP_HIDE_STOLEN_BYTES, (void*)HookTempHideMouseCursor, &trampoline, 5, MOD_NAME))
    {
        Log("Failed to hook TempHideMouseCursor at RVA 0x%X", RVA_TEMP_HIDE_MOUSE_CURSOR);
        InterlockedExchange(&g_installed, 0);
        return 0;
    }

    trampoline = NULL;

    if (!g_mj.InstallHook(RVA_GROUND_MOUSE_CURSOR_PIP_CTOR, PIP_CTOR_STOLEN_BYTES, (void*)HookGroundMouseCursorPipCtor, &trampoline, 20, MOD_NAME))
    {
        Log("Failed to hook GroundMouseCursorPip ctor at RVA 0x%X", RVA_GROUND_MOUSE_CURSOR_PIP_CTOR);
        InterlockedExchange(&g_installed, 0);
        return 0;
    }

    g_origGroundMouseCursorPipCtor = (fn_cursor_pip_ctor)trampoline;

    trampoline = NULL;

    if (!g_mj.InstallHook(RVA_MOUSE_CURSOR_PIP_3D_CTOR, PIP_CTOR_STOLEN_BYTES, (void*)HookMouseCursorPip3DCtor, &trampoline, 20, MOD_NAME))
    {
        Log("Failed to hook MouseCursorPip3D ctor at RVA 0x%X", RVA_MOUSE_CURSOR_PIP_3D_CTOR);
        InterlockedExchange(&g_installed, 0);
        return 0;
    }

    g_origMouseCursorPip3DCtor = (fn_cursor_pip_ctor)trampoline;

    trampoline = NULL;

    if (!g_mj.InstallHook(RVA_DISPLAY_OBJECT_SET_PROPERTY, DISPLAY_OBJECT_SET_PROPERTY_STOLEN_BYTES, (void*)HookDisplayObjectSetProperty, &trampoline, 30, MOD_NAME))
    {
        Log("Failed to hook DisplayObject set-property at RVA 0x%X", RVA_DISPLAY_OBJECT_SET_PROPERTY);
        InterlockedExchange(&g_installed, 0);
        return 0;
    }

    g_origDisplayObjectSetProperty = (fn_display_object_set_property)trampoline;

    StartForceHideTimer();
    Log("Installed hooks: TempHide=0x%X GroundCtor=0x%X Pip3DCtor=0x%X SetProp=0x%X", RVA_TEMP_HIDE_MOUSE_CURSOR, RVA_GROUND_MOUSE_CURSOR_PIP_CTOR, RVA_MOUSE_CURSOR_PIP_3D_CTOR, RVA_DISPLAY_OBJECT_SET_PROPERTY);

    return 1;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);

        if (MJ_Resolve(&g_mj) && g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "Loading!");
        }

        InstallHooks();
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        StopForceHideTimer();

        if (MJ_Resolve(&g_mj) && g_mj.Log)
        {
            g_mj.Log(MOD_NAME, "Unloading!");
        }
    }

    return TRUE;
}