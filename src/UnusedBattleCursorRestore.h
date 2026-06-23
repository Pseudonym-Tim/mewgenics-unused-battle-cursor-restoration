#ifndef UNUSED_BATTLE_CURSOR_RESTORE_H
#define UNUSED_BATTLE_CURSOR_RESTORE_H

#include <stdint.h>
#include <windows.h>

#define MOD_NAME "MewUnusedBattleCursorRestore"
#define ENABLE_DEBUG_LOGS 1

#define RVA_TEMP_HIDE_MOUSE_CURSOR                 0x009B0A40
#define RVA_GROUND_MOUSE_CURSOR_PIP_CTOR           0x0081D290
#define RVA_MOUSE_CURSOR_PIP_3D_CTOR               0x0081D4B0
#define RVA_DISPLAY_OBJECT_SET_PROPERTY            0x00991930

#define TEMP_HIDE_STOLEN_BYTES                     16
#define PIP_CTOR_STOLEN_BYTES                      19
#define DISPLAY_OBJECT_SET_PROPERTY_STOLEN_BYTES   14

#define DISPLAY_OBJECT_FLAGS_OFFSET                0x08
#define DISPLAY_OBJECT_VISIBLE_MASK                0x20
#define DISPLAY_OBJECT_BYTE0C_OFFSET               0x0C
#define DISPLAY_OBJECT_BYTE0D_OFFSET               0x0D
#define DISPLAY_OBJECT_PARENT_OFFSET               0x20

#define PARENT_CHILD_CAPACITY_OFFSET                0x288
#define PARENT_CHILD_COUNT_OFFSET                   0x28C
#define PARENT_CHILD_ARRAY_OFFSET                   0x290
#define PARENT_RENDER_CAPACITY_OFFSET               0x360
#define PARENT_RENDER_COUNT_OFFSET                  0x364
#define PARENT_RENDER_ARRAY_OFFSET                  0x368

#define MAX_TRACKED_PIPS                            64
#define FORCE_HIDE_POLL_INTERVAL_MS                 8U

typedef void  (__fastcall *fn_temp_hide_mouse_cursor)(void);
typedef void* (__fastcall *fn_cursor_pip_ctor)(void* a1, void* a2, void* a3);
typedef void  (__fastcall *fn_display_object_set_property)(void* object, void* propertyName, void* value);

#endif