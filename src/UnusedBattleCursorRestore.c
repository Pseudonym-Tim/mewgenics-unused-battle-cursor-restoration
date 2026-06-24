#include "UnusedBattleCursorRestore.h"

static MewjectorAPI g_mj;
static fn_cursor_pip_ctor g_origGroundMouseCursorPipCtor = 0;
static fn_cursor_pip_ctor g_origMouseCursorPip3DCtor = 0;
static void* g_trackedPips[MAX_TRACKED_PIPS];
static int g_installed = 0;
static fn_VirtualProtect g_VirtualProtect = 0;

typedef struct LIST_ENTRY64_S { struct LIST_ENTRY64_S* Flink; struct LIST_ENTRY64_S* Blink; } LIST_ENTRY64_S;
typedef struct UNICODE_STRING64_S { uint16_t Length; uint16_t MaximumLength; uint16_t* Buffer; } UNICODE_STRING64_S;

typedef struct LDR_DATA_TABLE_ENTRY64_S {
    LIST_ENTRY64_S InLoadOrderLinks;
    LIST_ENTRY64_S InMemoryOrderLinks;
    LIST_ENTRY64_S InInitializationOrderLinks;
    void* DllBase;
    void* EntryPoint;
    uint32_t SizeOfImage;
    UNICODE_STRING64_S FullDllName;
    UNICODE_STRING64_S BaseDllName;
} LDR_DATA_TABLE_ENTRY64_S;

typedef struct PEB_LDR_DATA64_S { uint32_t Length; uint8_t Initialized; uint8_t pad1[7]; void* SsHandle; LIST_ENTRY64_S InLoadOrderModuleList; LIST_ENTRY64_S InMemoryOrderModuleList; } PEB_LDR_DATA64_S;
typedef struct PEB64_S { uint8_t pad0[24]; PEB_LDR_DATA64_S* Ldr; } PEB64_S;

typedef struct IMAGE_DOS_HEADER_S { uint16_t e_magic; uint16_t pad[29]; uint32_t e_lfanew; } IMAGE_DOS_HEADER_S;
typedef struct IMAGE_FILE_HEADER_S { uint16_t Machine; uint16_t NumberOfSections; uint32_t TimeDateStamp; uint32_t PointerToSymbolTable; uint32_t NumberOfSymbols; uint16_t SizeOfOptionalHeader; uint16_t Characteristics; } IMAGE_FILE_HEADER_S;
typedef struct IMAGE_DATA_DIRECTORY_S { uint32_t VirtualAddress; uint32_t Size; } IMAGE_DATA_DIRECTORY_S;
typedef struct IMAGE_OPTIONAL_HEADER64_S { uint16_t Magic; uint8_t pad[110]; IMAGE_DATA_DIRECTORY_S DataDirectory[16]; } IMAGE_OPTIONAL_HEADER64_S;
typedef struct IMAGE_NT_HEADERS64_S { uint32_t Signature; IMAGE_FILE_HEADER_S FileHeader; IMAGE_OPTIONAL_HEADER64_S OptionalHeader; } IMAGE_NT_HEADERS64_S;

typedef struct IMAGE_EXPORT_DIRECTORY_S {
    uint32_t Characteristics; uint32_t TimeDateStamp; uint16_t MajorVersion; uint16_t MinorVersion; uint32_t Name; uint32_t Base; uint32_t NumberOfFunctions; uint32_t NumberOfNames; uint32_t AddressOfFunctions; uint32_t AddressOfNames; uint32_t AddressOfNameOrdinals;
} IMAGE_EXPORT_DIRECTORY_S;

#if defined(_MSC_VER)
unsigned __int64 __readgsqword(unsigned long Offset);
#endif

static PEB64_S* GetPeb(void)
{
#if defined(_MSC_VER)
    return (PEB64_S*)__readgsqword(0x60);
#else
    PEB64_S* peb;
    __asm__("movq %%gs:0x60, %0" : "=r"(peb));
    return peb;
#endif
}

static int lower_ascii(int c) { return (c >= 'A' && c <= 'Z') ? (c + 32) : c; }

static int eq_wide_ascii_ci(const uint16_t* w, uint16_t bytes, const char* s)
{
    uint32_t i, n = bytes / 2;
    for (i = 0; i < n && s[i]; ++i) if (lower_ascii((int)w[i]) != lower_ascii((int)s[i])) return 0;
    return i == n && s[i] == 0;
}

static int eq_ascii(const char* a, const char* b)
{
    while (*a && *b) { if (*a != *b) return 0; ++a; ++b; }
    return *a == *b;
}

static void* FindModuleBase(const char* dllName)
{
    PEB64_S* peb = GetPeb();
    LIST_ENTRY64_S* head;
    LIST_ENTRY64_S* cur;
    if (!peb || !peb->Ldr) return 0;
    head = &peb->Ldr->InMemoryOrderModuleList;
    cur = head->Flink;

    while (cur && cur != head)
    {
        LDR_DATA_TABLE_ENTRY64_S* e = (LDR_DATA_TABLE_ENTRY64_S*)((uint8_t*)cur - 16);
        if (e->BaseDllName.Buffer && eq_wide_ascii_ci(e->BaseDllName.Buffer, e->BaseDllName.Length, dllName)) return e->DllBase;
        cur = cur->Flink;
    }

    return 0;
}

static void* ResolveExport(void* moduleBase, const char* exportName)
{
    uint8_t* base = (uint8_t*)moduleBase;
    IMAGE_DOS_HEADER_S* dos;
    IMAGE_NT_HEADERS64_S* nt;
    IMAGE_EXPORT_DIRECTORY_S* exp;
    uint32_t* names;
    uint16_t* ords;
    uint32_t* funcs;
    uint32_t i;

    if (!base) return 0;
    dos = (IMAGE_DOS_HEADER_S*)base;
    if (dos->e_magic != 0x5A4D) return 0;
    nt = (IMAGE_NT_HEADERS64_S*)(base + dos->e_lfanew);
    if (nt->Signature != 0x00004550) return 0;
    if (!nt->OptionalHeader.DataDirectory[0].VirtualAddress) return 0;
    exp = (IMAGE_EXPORT_DIRECTORY_S*)(base + nt->OptionalHeader.DataDirectory[0].VirtualAddress);
    names = (uint32_t*)(base + exp->AddressOfNames);
    ords = (uint16_t*)(base + exp->AddressOfNameOrdinals);
    funcs = (uint32_t*)(base + exp->AddressOfFunctions);

    for (i = 0; i < exp->NumberOfNames; ++i)
    {
        char* name = (char*)(base + names[i]);
        if (eq_ascii(name, exportName)) return base + funcs[ords[i]];
    }

    return 0;
}

static int ResolveRuntime(void)
{
    void* kernel32;
    void* version;
    if (g_VirtualProtect && g_mj.InstallHook && g_mj.GetGameBase) return 1;
    kernel32 = FindModuleBase("KERNEL32.DLL");
    if (!kernel32) kernel32 = FindModuleBase("kernel32.dll");
    g_VirtualProtect = (fn_VirtualProtect)ResolveExport(kernel32, "VirtualProtect");

    version = FindModuleBase("version.dll");
    if (!version) version = FindModuleBase("VERSION.DLL");
    g_mj.GetVersion = (MJ_fn_GetVersion)ResolveExport(version, "MJ_GetVersion");
    if (!g_mj.GetVersion || g_mj.GetVersion() < 3) return 0;
    g_mj.InstallHook = (MJ_fn_InstallHook)ResolveExport(version, "MJ_InstallHook");
    g_mj.GetGameBase = (MJ_fn_GetGameBase)ResolveExport(version, "MJ_GetGameBase");
    g_mj.Log = (MJ_fn_Log)ResolveExport(version, "MJ_Log");

    return g_VirtualProtect && g_mj.InstallHook && g_mj.GetGameBase;
}

static void Log0(const char* s)
{
    if (g_mj.Log) g_mj.Log(MOD_NAME, "%s", s);
}

static int PatchBytes(uint32_t rva, const uint8_t* bytes, uint32_t count)
{
    uint8_t* addr;
    DWORD oldProtect = 0, ignored = 0;
    if (!ResolveRuntime()) return 0;
    addr = (uint8_t*)g_mj.GetGameBase() + rva;
    if (!g_VirtualProtect(addr, count, PAGE_EXECUTE_READWRITE, &oldProtect)) return 0;
    for (uint32_t i = 0; i < count; ++i) addr[i] = bytes[i];
    g_VirtualProtect(addr, count, oldProtect, &ignored);

    return 1;
}

static void InstallCodePatches(void)
{
    uint8_t ret = 0xC3;
    uint8_t visible = 0x01;

    // Full suppression of glaiel::TempHideMouseCursor, hopefully this doesn't break anything...
    PatchBytes(RVA_TEMP_HIDE_MOUSE_CURSOR, &ret, 1);

    // Cursor manager state 3 uses a countdown and clears cursor+0x38 when it expires... 
    // Keep the Mew cursor visible instead of auto-hiding after hover...
    PatchBytes(RVA_CURSOR_IDLE_TIMEOUT_HIDE_BYTE, &visible, 1);
}

static void AddTrackedPip(void* object)
{
    if (!object) return;
    for (int i = 0; i < MAX_TRACKED_PIPS; ++i) if (g_trackedPips[i] == object) return;
    for (int i = 0; i < MAX_TRACKED_PIPS; ++i) if (!g_trackedPips[i]) { g_trackedPips[i] = object; return; }
    g_trackedPips[0] = object;
}

static int RemovePointerFromPackedArray(void** arrayBase, uint32_t* countPtr, void* object)
{
    uint32_t count, readIndex, writeIndex;
    int removed = 0;
    if (!arrayBase || !countPtr || !object) return 0;
    count = *countPtr;
    if (count > 4096u) return 0;
    writeIndex = 0;

    for (readIndex = 0; readIndex < count; ++readIndex)
    {
        void* entry = arrayBase[readIndex];
        if (entry == object) { removed = 1; continue; }
        if (writeIndex != readIndex) arrayBase[writeIndex] = entry;
        ++writeIndex;
    }

    if (removed)
    {
        for (uint32_t i = writeIndex; i < count; ++i) arrayBase[i] = 0;
        *countPtr = writeIndex;
    }

    return removed;
}

static void KillPipObject(void* object)
{
    uint8_t* child = (uint8_t*)object;
    uint8_t* parent;
    int removed = 0;
    if (!object) return;

    child[DISPLAY_OBJECT_FLAGS_OFFSET] &= (uint8_t)~DISPLAY_OBJECT_VISIBLE_MASK;
    child[DISPLAY_OBJECT_BYTE0C_OFFSET] = 0;
    child[DISPLAY_OBJECT_BYTE0D_OFFSET] &= (uint8_t)0x8F;

    parent = *(uint8_t**)(child + DISPLAY_OBJECT_PARENT_OFFSET);
    if (!parent) return;
    removed |= RemovePointerFromPackedArray(*(void***)(parent + PARENT_CHILD_ARRAY_OFFSET), (uint32_t*)(parent + PARENT_CHILD_COUNT_OFFSET), object);
    removed |= RemovePointerFromPackedArray(*(void***)(parent + PARENT_RENDER_ARRAY_OFFSET), (uint32_t*)(parent + PARENT_RENDER_COUNT_OFFSET), object);
    if (removed) *(void**)(child + DISPLAY_OBJECT_PARENT_OFFSET) = 0;
}

static void* __fastcall HookGroundMouseCursorPipCtor(void* a1, void* a2, void* a3)
{
    void* object = g_origGroundMouseCursorPipCtor ? g_origGroundMouseCursorPipCtor(a1, a2, a3) : 0;
    if (object) { AddTrackedPip(object); KillPipObject(object); }
    return object;
}

static void* __fastcall HookMouseCursorPip3DCtor(void* a1, void* a2, void* a3)
{
    void* object = g_origMouseCursorPip3DCtor ? g_origMouseCursorPip3DCtor(a1, a2, a3) : 0;
    if (object) { AddTrackedPip(object); KillPipObject(object); }
    return object;
}

static void InstallHooks(void)
{
    void* trampoline = 0;
    if (g_installed || !ResolveRuntime()) return;
    g_installed = 1;

    InstallCodePatches();

    if (g_mj.InstallHook(RVA_GROUND_MOUSE_CURSOR_PIP_CTOR, PIP_CTOR_STOLEN_BYTES, (void*)HookGroundMouseCursorPipCtor, &trampoline, 20, MOD_NAME))
    {
        g_origGroundMouseCursorPipCtor = (fn_cursor_pip_ctor)trampoline;
    }

    trampoline = 0;

    if (g_mj.InstallHook(RVA_MOUSE_CURSOR_PIP_3D_CTOR, PIP_CTOR_STOLEN_BYTES, (void*)HookMouseCursorPip3DCtor, &trampoline, 20, MOD_NAME))
    {
        g_origMouseCursorPip3DCtor = (fn_cursor_pip_ctor)trampoline;
    }

    Log0("Installed cursor restoration hooks and idle-timeout visibility patch...");
}

BOOL __stdcall DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    (void)hModule; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) InstallHooks();
    return TRUE;
}