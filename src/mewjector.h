/* mewjector.h — Mewjector API header for mod developers */
#ifndef MEWJECTOR_H
#define MEWJECTOR_H

#include <windows.h>
#include <string.h>

#define MJ_API_VERSION 3

typedef int (__cdecl *MJ_fn_InstallHook)(UINT_PTR rva, int stolenBytes, void* hookFn, void** outTrampoline, int priority, const char* owner);
typedef int (__cdecl *MJ_fn_QueryHook)(UINT_PTR rva);
typedef UINT_PTR (__cdecl *MJ_fn_AllocTypeIdPair)(const char* owner);
typedef int (__cdecl *MJ_fn_RegisterName)(const char* category, const char* name, const char* owner);
typedef const char* (__cdecl *MJ_fn_LookupName)(const char* category, const char* name);
typedef UINT_PTR (__cdecl *MJ_fn_GetGameBase)(void);
typedef void (__cdecl *MJ_fn_Log)(const char* owner, const char* fmt, ...);
typedef int (__cdecl *MJ_fn_VerifyHooks)(void);
typedef int (__cdecl *MJ_fn_GetVersion)(void);

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

static inline int MJ_Resolve(MewjectorAPI* api)
{
    if (!api) return 0;
    memset(api, 0, sizeof(MewjectorAPI));

    HMODULE hMJ = GetModuleHandleA("version.dll");
    if (!hMJ) return 0;

    api->GetVersion = (MJ_fn_GetVersion)GetProcAddress(hMJ, "MJ_GetVersion");
    if (!api->GetVersion) return 0;
    if (api->GetVersion() < MJ_API_VERSION) return 0;

#define MJ__RESOLVE(field, name) \
    api->field = (MJ_fn_##field)GetProcAddress(hMJ, "MJ_" name); \
    if (!api->field) return 0

    MJ__RESOLVE(InstallHook,     "InstallHook");
    MJ__RESOLVE(QueryHook,       "QueryHook");
    MJ__RESOLVE(AllocTypeIdPair, "AllocTypeIdPair");
    MJ__RESOLVE(RegisterName,    "RegisterName");
    MJ__RESOLVE(LookupName,      "LookupName");
    MJ__RESOLVE(GetGameBase,     "GetGameBase");
    MJ__RESOLVE(Log,             "Log");
    MJ__RESOLVE(VerifyHooks,     "VerifyHooks");

#undef MJ__RESOLVE
    return 1;
}

#endif /* MEWJECTOR_H */
