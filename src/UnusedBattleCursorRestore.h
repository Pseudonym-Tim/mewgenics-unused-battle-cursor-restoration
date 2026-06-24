#ifndef UNUSED_BATTLE_CURSOR_RESTORE_H
#define UNUSED_BATTLE_CURSOR_RESTORE_H

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned long long uint64_t;
typedef unsigned long long UINT_PTR;
typedef unsigned long DWORD;
typedef int BOOL;
typedef void* PVOID;
typedef void* HMODULE;
typedef void* LPVOID;

#define TRUE 1
#define FALSE 0
#define DLL_PROCESS_ATTACH 1
#define DLL_PROCESS_DETACH 0
#define PAGE_EXECUTE_READWRITE 0x40

#define MOD_NAME "UnusedBattleCursorRestore"

#define RVA_TEMP_HIDE_MOUSE_CURSOR                 0x009B0A40u
#define RVA_GROUND_MOUSE_CURSOR_PIP_CTOR           0x0081D290u
#define RVA_MOUSE_CURSOR_PIP_3D_CTOR               0x0081D4B0u
#define RVA_CURSOR_IDLE_TIMEOUT_HIDE_BYTE          0x0097E801u

#define PIP_CTOR_STOLEN_BYTES                      19

#define DISPLAY_OBJECT_FLAGS_OFFSET                0x08
#define DISPLAY_OBJECT_VISIBLE_MASK                0x20
#define DISPLAY_OBJECT_BYTE0C_OFFSET               0x0C
#define DISPLAY_OBJECT_BYTE0D_OFFSET               0x0D
#define DISPLAY_OBJECT_PARENT_OFFSET               0x20

#define PARENT_CHILD_COUNT_OFFSET                  0x28C
#define PARENT_CHILD_ARRAY_OFFSET                  0x290
#define PARENT_RENDER_COUNT_OFFSET                 0x364
#define PARENT_RENDER_ARRAY_OFFSET                 0x368

#define MAX_TRACKED_PIPS                           64

typedef int      (__cdecl *MJ_fn_InstallHook)(UINT_PTR rva, int stolenBytes, void* hookFn, void** outTrampoline, int priority, const char* owner);
typedef UINT_PTR (__cdecl *MJ_fn_GetGameBase)(void);
typedef int      (__cdecl *MJ_fn_GetVersion)(void);
typedef void     (__cdecl *MJ_fn_Log)(const char* owner, const char* fmt, ...);

typedef struct {
    MJ_fn_InstallHook InstallHook;
    void* QueryHook;
    void* AllocTypeIdPair;
    void* RegisterName;
    void* LookupName;
    MJ_fn_GetGameBase GetGameBase;
    MJ_fn_Log Log;
    void* VerifyHooks;
    MJ_fn_GetVersion GetVersion;
} MewjectorAPI;

typedef void* (__fastcall *fn_cursor_pip_ctor)(void* a1, void* a2, void* a3);
typedef int (__stdcall *fn_VirtualProtect)(void* address, uint64_t size, DWORD newProtect, DWORD* oldProtect);

#endif