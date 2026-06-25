#include <stdint.h>
#include <stddef.h>

#ifdef _MSC_VER
#include <intrin.h>
#define MSABI __fastcall
#define STDCALL __stdcall
#else
#define MSABI __attribute__((ms_abi))
#define STDCALL __attribute__((stdcall))
#endif

#define MOD_NAME "UnusedBattleCursorRestore"

#define RVA_TEMP_HIDE_MOUSE_CURSOR                 0x009B0A40u  // glaiel::TempHideMouseCursor entry...
#define RVA_BATTLE_CURSOR_INIT                     0x00816F60u  // Battle cursor owner initialization...
#define RVA_GROUND_MOUSE_CURSOR_PIP_CTOR           0x0081D290u  // GroundMouseCursorPip constructor...
#define RVA_MOUSE_CURSOR_PIP_3D_CTOR               0x0081D4B0u  // MouseCursorPip3D constructor...
#define RVA_DISPLAY_OBJECT_SET_PROPERTY            0x00991930u  // DisplayObject property setter...
#define RVA_CURSOR_IDLE_TIMEOUT_HIDE_BYTE          0x0097E801u  // Byte in idle-timeout visibility write path...

#define TEMP_HIDE_STOLEN_BYTES                     16 // Prologue byte count overwritten by the TempHideMouseCursor trampoline hook...
#define BATTLE_CURSOR_INIT_STOLEN_BYTES            15 // Prologue byte count overwritten by the BattleCursorInit trampoline hook...
#define PIP_CTOR_STOLEN_BYTES                      19 // Prologue byte count overwritten by each cursor-pip constructor trampoline hook...
#define DISPLAY_OBJECT_SET_PROPERTY_STOLEN_BYTES   14 // Prologue byte count overwritten by the DisplayObject set-property trampoline hook...

#define DISPLAY_OBJECT_FLAGS_OFFSET                0x08 // DisplayObject flags byte...
#define DISPLAY_OBJECT_VISIBLE_MASK                0x20 // Visibility bit inside DisplayObject flags...
#define DISPLAY_OBJECT_BYTE0C_OFFSET               0x0C // DisplayObject state byte...
#define DISPLAY_OBJECT_BYTE0D_OFFSET               0x0D // DisplayObject state/flag byte...
#define DISPLAY_OBJECT_PARENT_OFFSET               0x20 // Parent DisplayObject/container pointer...

#define PARENT_CHILD_COUNT_OFFSET                   0x28C // Parent child-list count...
#define PARENT_CHILD_ARRAY_OFFSET                   0x290 // Parent child-list pointer array containing DisplayObject children, including cursor pips...
#define PARENT_RENDER_COUNT_OFFSET                  0x364 // Parent render-list count used when removing cursor pips from the renderer traversal list...
#define PARENT_RENDER_ARRAY_OFFSET                  0x368 // Parent render-list pointer array containing objects queued for rendering...

#define CURSOR_STATE_OFFSET                         0x54 // Cursor pip state field...
#define CURSOR_ENABLE_FLAGS_OFFSET                  0x58 // Cursor pip enable-flags field...

#define OWNER_GROUND_CURSOR_OFFSET                  0x50 // Battle cursor owner pointer to the original ground cursor pip...
#define OWNER_PIP3D_CURSOR_OFFSET                   0x58 // Battle cursor owner pointer to the original 3D cursor pip...

#define MAX_TRACKED_PIPS                            128 // Fixed-size tracking table for cursor pip objects that may need repeated neutralization...
#define MJ_API_VERSION                              3 // Minimum Mewjector API version required for no-import hook installation...
#define PAGE_EXECUTE_READWRITE                      0x40u // Windows page-protection value...

typedef void* HMODULE;
typedef void* LPVOID;
typedef unsigned long DWORD;
typedef int BOOL;
typedef unsigned long long UINT_PTR;

typedef int (__cdecl *MJ_fn_InstallHook)(UINT_PTR rva, int stolenBytes, void* hookFn, void** outTrampoline, int priority, const char* owner);
typedef int (__cdecl *MJ_fn_QueryHook)(UINT_PTR rva);
typedef UINT_PTR (__cdecl *MJ_fn_AllocTypeIdPair)(const char* owner);
typedef int (__cdecl *MJ_fn_RegisterName)(const char* category, const char* name, const char* owner);
typedef const char* (__cdecl *MJ_fn_LookupName)(const char* category, const char* name);
typedef UINT_PTR (__cdecl *MJ_fn_GetGameBase)(void);
typedef void (__cdecl *MJ_fn_Log)(const char* owner, const char* fmt, ...);
typedef int (__cdecl *MJ_fn_VerifyHooks)(void);
typedef int (__cdecl *MJ_fn_GetVersion)(void);
typedef int (STDCALL *fn_VirtualProtect)(void* address, size_t size, DWORD newProtect, DWORD* oldProtect);

typedef struct {
    MJ_fn_InstallHook     InstallHook;
    MJ_fn_QueryHook       QueryHook;
    MJ_fn_AllocTypeIdPair AllocTypeIdPair;
    MJ_fn_RegisterName    RegisterName;
    MJ_fn_LookupName      LookupName;
    MJ_fn_GetGameBase     GetGameBase;
    MJ_fn_Log             Log;
    MJ_fn_VerifyHooks     VerifyHooks;
    MJ_fn_GetVersion      GetVersion;
} MewjectorAPI;

typedef void  (MSABI *fn_temp_hide_mouse_cursor)(void);
typedef void* (MSABI *fn_one_arg)(void* self);
typedef void* (MSABI *fn_cursor_pip_ctor)(void* a1, void* a2, void* a3);
typedef void  (MSABI *fn_display_object_set_property)(void* object, void* propertyName, void* value);

static MewjectorAPI g_mj;
static volatile int g_installed = 0;
static fn_one_arg g_origBattleCursorInit = 0;
static fn_cursor_pip_ctor g_origGroundMouseCursorPipCtor = 0;
static fn_cursor_pip_ctor g_origMouseCursorPip3DCtor = 0;
static fn_display_object_set_property g_origDisplayObjectSetProperty = 0;
static void* volatile g_trackedPips[MAX_TRACKED_PIPS];
static void* volatile g_knownGroundVtable = 0;
static void* volatile g_knownPip3DVtable = 0;
static volatile int g_loggedTempHide = 0;
static volatile int g_loggedInitKill = 0;
static volatile int g_loggedCtorKill = 0;
static volatile int g_loggedVtableKill = 0;
static volatile int g_loggedIdlePatch = 0;
static fn_VirtualProtect g_VirtualProtect = 0;

// Tiny runtime helpers...
static void* local_memset(void* p, int v, size_t n) { unsigned char* b=(unsigned char*)p; while (n--) *b++=(unsigned char)v; return p; }
static void memzero(void* p, size_t n) { local_memset(p, 0, n); }
static int streq(const char* a, const char* b) { while (*a && *b && *a==*b) { ++a; ++b; } return *a==*b; }
static char lower_ascii_w(uint16_t c) { if (c>='A' && c<='Z') c += 32; return (char)c; }

static int basename_is_version_dll(const uint16_t* s, unsigned short bytes)
{
    static const char want[] = "version.dll";
    unsigned n = bytes / 2;
    if (n != 11) return 0;
    for (unsigned i=0; i<11; ++i) if (lower_ascii_w(s[i]) != want[i]) return 0;
    return 1;
}

static int basename_equals_ascii_ci(const uint16_t* s, unsigned short bytes, const char* want)
{
    unsigned n = bytes / 2;
    unsigned i = 0;
    if (!s || !want) return 0;
    for (; i < n && want[i]; ++i)
        if (lower_ascii_w(s[i]) != lower_ascii_w((uint16_t)want[i])) return 0;
    return i == n && want[i] == 0;
}

#if defined(_MSC_VER)
static void* read_peb(void) { return (void*)__readgsqword(0x60); }
#elif defined(__x86_64__)
static void* read_peb(void)
{
    void* p;
    __asm__("movq %%gs:0x60, %0" : "=r"(p));
    return p;
}
#endif

typedef struct LIST_ENTRY_T { struct LIST_ENTRY_T* Flink; struct LIST_ENTRY_T* Blink; } LIST_ENTRY_T;
typedef struct UNICODE_STRING_T { unsigned short Length; unsigned short MaximumLength; uint16_t* Buffer; } UNICODE_STRING_T;
typedef struct LDR_DATA_TABLE_ENTRY_T {
    LIST_ENTRY_T InLoadOrderLinks;
    LIST_ENTRY_T InMemoryOrderLinks;
    LIST_ENTRY_T InInitializationOrderLinks;
    void* DllBase;
    void* EntryPoint;
    unsigned long SizeOfImage;
    UNICODE_STRING_T FullDllName;
    UNICODE_STRING_T BaseDllName;
} LDR_DATA_TABLE_ENTRY_T;

typedef struct PEB_LDR_DATA_T { unsigned long Length; unsigned char Initialized; void* SsHandle; LIST_ENTRY_T InLoadOrderModuleList; } PEB_LDR_DATA_T;
typedef struct PEB_T { unsigned char Reserved1[24]; PEB_LDR_DATA_T* Ldr; } PEB_T;

static void* find_loaded_version_dll(void)
{
    PEB_T* peb = (PEB_T*)read_peb();
    if (!peb || !peb->Ldr) return 0;
    LIST_ENTRY_T* head = &peb->Ldr->InLoadOrderModuleList;

    for (LIST_ENTRY_T* e = head->Flink; e && e != head; e = e->Flink)
    {
        LDR_DATA_TABLE_ENTRY_T* ent = (LDR_DATA_TABLE_ENTRY_T*)e;

        if (ent->BaseDllName.Buffer && basename_is_version_dll(ent->BaseDllName.Buffer, ent->BaseDllName.Length))
        {
            return ent->DllBase;
        }
    }

    return 0;
}

static void* find_loaded_module_ascii(const char* basename)
{
    PEB_T* peb = (PEB_T*)read_peb();
    if (!peb || !peb->Ldr) return 0;
    LIST_ENTRY_T* head = &peb->Ldr->InLoadOrderModuleList;

    for (LIST_ENTRY_T* e = head->Flink; e && e != head; e = e->Flink)
    {
        LDR_DATA_TABLE_ENTRY_T* ent = (LDR_DATA_TABLE_ENTRY_T*)e;

        if (ent->BaseDllName.Buffer && basename_equals_ascii_ci(ent->BaseDllName.Buffer, ent->BaseDllName.Length, basename))
        {
            return ent->DllBase;
        }
    }

    return 0;
}

static uint32_t read_u32(const unsigned char* p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint16_t read_u16(const unsigned char* p) { return (uint16_t)p[0] | ((uint16_t)p[1]<<8); }

static void* pe_export(void* module, const char* name)
{
    unsigned char* base = (unsigned char*)module;
    if (!base || read_u16(base) != 0x5A4D) return 0;
    uint32_t peoff = read_u32(base + 0x3C);
    unsigned char* pe = base + peoff;
    if (read_u32(pe) != 0x00004550) return 0;
    uint16_t optMagic = read_u16(pe + 0x18);
    uint32_t exportRva = 0;
    if (optMagic == 0x20B) exportRva = read_u32(pe + 0x18 + 0x70);
    else if (optMagic == 0x10B) exportRva = read_u32(pe + 0x18 + 0x60);
    if (!exportRva) return 0;
    unsigned char* exp = base + exportRva;
    uint32_t numberOfNames = read_u32(exp + 0x18);
    uint32_t funcsRva = read_u32(exp + 0x1C);
    uint32_t namesRva = read_u32(exp + 0x20);
    uint32_t ordsRva  = read_u32(exp + 0x24);
    uint32_t* funcs = (uint32_t*)(base + funcsRva);
    uint32_t* names = (uint32_t*)(base + namesRva);
    uint16_t* ords = (uint16_t*)(base + ordsRva);

    for (uint32_t i=0; i<numberOfNames; ++i)
    {
        const char* n = (const char*)(base + names[i]);
        if (streq(n, name)) return base + funcs[ords[i]];
    }

    return 0;
}

static int MJ_Resolve_NoImports(MewjectorAPI* api)
{
    void* h = find_loaded_version_dll();

    if (!api || !h) return 0;

    memzero(api, sizeof(*api));
    api->GetVersion = (MJ_fn_GetVersion)pe_export(h, "MJ_GetVersion");

    if (!api->GetVersion || api->GetVersion() < MJ_API_VERSION) return 0;

    api->InstallHook     = (MJ_fn_InstallHook)pe_export(h, "MJ_InstallHook");
    api->QueryHook       = (MJ_fn_QueryHook)pe_export(h, "MJ_QueryHook");
    api->AllocTypeIdPair = (MJ_fn_AllocTypeIdPair)pe_export(h, "MJ_AllocTypeIdPair");
    api->RegisterName    = (MJ_fn_RegisterName)pe_export(h, "MJ_RegisterName");
    api->LookupName      = (MJ_fn_LookupName)pe_export(h, "MJ_LookupName");
    api->GetGameBase     = (MJ_fn_GetGameBase)pe_export(h, "MJ_GetGameBase");
    api->Log             = (MJ_fn_Log)pe_export(h, "MJ_Log");
    api->VerifyHooks     = (MJ_fn_VerifyHooks)pe_export(h, "MJ_VerifyHooks");

    return api->InstallHook && api->Log;
}

static void LogFixed(const char* msg)
{
    if (g_mj.Log) g_mj.Log(MOD_NAME, "%s", msg);
}

static int ResolveVirtualProtect_NoImports(void)
{
    void* k32;
    if (g_VirtualProtect) return 1;
    k32 = find_loaded_module_ascii("kernel32.dll");
    if (!k32) k32 = find_loaded_module_ascii("KERNEL32.DLL");
    g_VirtualProtect = (fn_VirtualProtect)pe_export(k32, "VirtualProtect");

    if (!g_VirtualProtect)
    {
        void* kb = find_loaded_module_ascii("kernelbase.dll");
        if (!kb) kb = find_loaded_module_ascii("KERNELBASE.DLL");
        g_VirtualProtect = (fn_VirtualProtect)pe_export(kb, "VirtualProtect");
    }

    return g_VirtualProtect != 0;
}

static int PatchGameByte(uint32_t rva, unsigned char value)
{
    DWORD oldProtect = 0;
    DWORD ignoredProtect = 0;
    unsigned char* addr;
    if (!g_mj.GetGameBase || !ResolveVirtualProtect_NoImports()) return 0;
    addr = (unsigned char*)g_mj.GetGameBase() + rva;
    if (!g_VirtualProtect(addr, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) return 0;
    *addr = value;
    g_VirtualProtect(addr, 1, oldProtect, &ignoredProtect);

    return 1;
}

static void InstallIdleTimeoutVisibilityPatch(void)
{
    // Force mew cursor to stray visible while hovering idle...
    if (PatchGameByte(RVA_CURSOR_IDLE_TIMEOUT_HIDE_BYTE, 0x01))
    {
        if (!g_loggedIdlePatch) { g_loggedIdlePatch = 1; LogFixed("Patched cursor idle-timeout visibility write"); }
    }
    else
    {
        LogFixed("WARNING: failed to patch cursor idle-timeout visibility write");
    }
}

static void AddTrackedPip(void* object)
{
    if (!object) return;
    for (int i=0; i<MAX_TRACKED_PIPS; ++i) if (g_trackedPips[i] == object) return;
    for (int i=0; i<MAX_TRACKED_PIPS; ++i) if (!g_trackedPips[i]) { g_trackedPips[i] = object; return; }
    g_trackedPips[0] = object;
}

static int IsTrackedPip(void* object)
{
    if (!object) return 0;
    for (int i=0; i<MAX_TRACKED_PIPS; ++i) if (g_trackedPips[i] == object) return 1;
    return 0;
}

static void RememberVtable(void* object, void* volatile* slot)
{
    if (object && slot && !*slot) *slot = *(void**)object;
}

static int IsKnownCursorPipObject(void* object)
{
    if (!object) return 0;
    void* vt = *(void**)object;
    return vt && (vt == g_knownGroundVtable || vt == g_knownPip3DVtable);
}

static int RemovePointerFromPackedArray(void** arrayBase, uint32_t* countPtr, void* object)
{
    if (!arrayBase || !countPtr || !object) return 0;
    uint32_t count = *countPtr;
    if (count > 8192) return 0;
    uint32_t w = 0; int removed = 0;

    for (uint32_t r=0; r<count; ++r)
    {
        void* entry = arrayBase[r];
        if (entry == object) { removed = 1; continue; }
        if (w != r) arrayBase[w] = entry;
        ++w;
    }

    if (removed)
    {
        for (uint32_t i=w; i<count; ++i) arrayBase[i] = 0;
        *countPtr = w;
    }

    return removed;
}

static void ForceNeutralCursorObject(void* object)
{
    if (!object) return;
    unsigned char* b = (unsigned char*)object;
    b[DISPLAY_OBJECT_FLAGS_OFFSET] &= (unsigned char)~DISPLAY_OBJECT_VISIBLE_MASK;
    b[DISPLAY_OBJECT_BYTE0C_OFFSET] = 0;
    b[DISPLAY_OBJECT_BYTE0D_OFFSET] &= (unsigned char)0x8F;
    *(uint32_t*)(b + CURSOR_STATE_OFFSET) = 0;
    *(uint32_t*)(b + CURSOR_ENABLE_FLAGS_OFFSET) &= ~1u;
}

static void UnlinkFromParentLists(void* object)
{
    if (!object) return;
    unsigned char* child = (unsigned char*)object;
    unsigned char* parent = *(unsigned char**)(child + DISPLAY_OBJECT_PARENT_OFFSET);
    if (!parent) return;

    int removed = 0;
    removed |= RemovePointerFromPackedArray(*(void***)(parent + PARENT_CHILD_ARRAY_OFFSET),  (uint32_t*)(parent + PARENT_CHILD_COUNT_OFFSET),  object);
    removed |= RemovePointerFromPackedArray(*(void***)(parent + PARENT_RENDER_ARRAY_OFFSET), (uint32_t*)(parent + PARENT_RENDER_COUNT_OFFSET), object);
    if (removed) *(void**)(child + DISPLAY_OBJECT_PARENT_OFFSET) = 0;
}

static void KillPipObject(void* object)
{
    if (!object) return;
    ForceNeutralCursorObject(object);
    UnlinkFromParentLists(object);
    ForceNeutralCursorObject(object);
}

static void KillKnownPips(void)
{
    for (int i = 0; i < MAX_TRACKED_PIPS; ++i) if (g_trackedPips[i]) KillPipObject(g_trackedPips[i]);
}

static void KillOwnerCursorFields(void* owner)
{
    if (!owner) return;
    unsigned char* o = (unsigned char*)owner;
    void* ground = *(void**)(o + OWNER_GROUND_CURSOR_OFFSET);
    void* pip3d  = *(void**)(o + OWNER_PIP3D_CURSOR_OFFSET);
    if (ground) { AddTrackedPip(ground); RememberVtable(ground, &g_knownGroundVtable); KillPipObject(ground); }
    if (pip3d)  { AddTrackedPip(pip3d);  RememberVtable(pip3d,  &g_knownPip3DVtable);  KillPipObject(pip3d); }
}

static void MSABI HookTempHideMouseCursor(void)
{
    if (!g_loggedTempHide) { g_loggedTempHide = 1; LogFixed("Suppressed glaiel::TempHideMouseCursor"); }
    KillKnownPips();
}

static void* MSABI HookBattleCursorInit(void* self)
{
    void* ret = 0;
    if (g_origBattleCursorInit) ret = g_origBattleCursorInit(self);
    KillOwnerCursorFields(self);
    KillKnownPips();
    if (!g_loggedInitKill) { g_loggedInitKill = 1; LogFixed("Post-init neutralized original 3D cursor pips without returning NULL"); }
    return ret;
}

static void* MSABI HookGroundMouseCursorPipCtor(void* a1, void* a2, void* a3)
{
    void* object = g_origGroundMouseCursorPipCtor ? g_origGroundMouseCursorPipCtor(a1,a2,a3) : 0;

    if (object)
    {
        AddTrackedPip(object);
        RememberVtable(object, &g_knownGroundVtable);
        KillPipObject(object);
        if (!g_loggedCtorKill) { g_loggedCtorKill = 1; LogFixed("Constructed original cursor pip, then immediately neutralized it"); }
    }

    return object;
}

static void* MSABI HookMouseCursorPip3DCtor(void* a1, void* a2, void* a3)
{
    void* object = g_origMouseCursorPip3DCtor ? g_origMouseCursorPip3DCtor(a1,a2,a3) : 0;

    if (object)
    {
        AddTrackedPip(object);
        RememberVtable(object, &g_knownPip3DVtable);
        KillPipObject(object);
        if (!g_loggedCtorKill) { g_loggedCtorKill = 1; LogFixed("Constructed original cursor pip, then immediately neutralized it"); }
    }

    return object;
}

static void MSABI HookDisplayObjectSetProperty(void* object, void* propertyName, void* value)
{
    if (g_origDisplayObjectSetProperty) g_origDisplayObjectSetProperty(object, propertyName, value);

    if (IsTrackedPip(object) || IsKnownCursorPipObject(object))
    {
        AddTrackedPip(object);
        KillPipObject(object);
        if (!g_loggedVtableKill) { g_loggedVtableKill = 1; LogFixed("Neutralized cursor pip after display-property mutation"); }
    }
}

static int InstallHooks(void)
{
    void* trampoline = 0;
    if (!MJ_Resolve_NoImports(&g_mj)) return 0;
    if (g_installed) return 1;
    g_installed = 1;

    InstallIdleTimeoutVisibilityPatch();

    if (!g_mj.InstallHook(RVA_TEMP_HIDE_MOUSE_CURSOR, TEMP_HIDE_STOLEN_BYTES, (void*)HookTempHideMouseCursor, &trampoline, 5, MOD_NAME)) return 0;

    trampoline = 0;

    if (g_mj.InstallHook(RVA_BATTLE_CURSOR_INIT, BATTLE_CURSOR_INIT_STOLEN_BYTES, (void*)HookBattleCursorInit, &trampoline, 15, MOD_NAME))
    {
        g_origBattleCursorInit = (fn_one_arg)trampoline;
    }
    else
    {
        LogFixed("WARNING: failed to hook battle cursor init; falling back to ctor/property hooks only");
    }

    trampoline = 0;

    if (g_mj.InstallHook(RVA_GROUND_MOUSE_CURSOR_PIP_CTOR, PIP_CTOR_STOLEN_BYTES, (void*)HookGroundMouseCursorPipCtor, &trampoline, 20, MOD_NAME))
    {
        g_origGroundMouseCursorPipCtor = (fn_cursor_pip_ctor)trampoline;
    }
    else
    {
        LogFixed("WARNING: failed to hook GroundMouseCursorPip ctor");
    }

    trampoline = 0;

    if (g_mj.InstallHook(RVA_MOUSE_CURSOR_PIP_3D_CTOR, PIP_CTOR_STOLEN_BYTES, (void*)HookMouseCursorPip3DCtor, &trampoline, 20, MOD_NAME))
    {
        g_origMouseCursorPip3DCtor = (fn_cursor_pip_ctor)trampoline;
    }
    else
    {
        LogFixed("WARNING: failed to hook MouseCursorPip3D ctor");
    }

    trampoline = 0;

    if (g_mj.InstallHook(RVA_DISPLAY_OBJECT_SET_PROPERTY, DISPLAY_OBJECT_SET_PROPERTY_STOLEN_BYTES, (void*)HookDisplayObjectSetProperty, &trampoline, 30, MOD_NAME))
    {
        g_origDisplayObjectSetProperty = (fn_display_object_set_property)trampoline;
    }
    else
    {
        LogFixed("WARNING: failed to hook DisplayObject set-property");
    }

    LogFixed("Patch successful!");

    return 1;
}

BOOL STDCALL DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    (void)hModule; (void)reserved;

    if (reason == 1)
    {
        InstallHooks();
        if (g_mj.Log) LogFixed("Loading!");
    }

    return 1;
}