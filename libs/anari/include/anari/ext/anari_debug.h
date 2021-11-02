// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anari/ext/anari_ext_interface.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*PFNANARIINSERTSTATUSMESSAGE)(ANARIDevice, const char*);
typedef void (*PFNANARINAMEOBJECT)(ANARIDevice, ANARIObject, const char*);

typedef struct ANARI_EXT_debug_interface_s {
    PFNANARIINSERTSTATUSMESSAGE anariInsertStatusMessage;
    PFNANARINAMEOBJECT anariNameObject;
} ANARI_EXT_debug_interface;

static inline int init_ANARI_EXT_debug_interface(ANARIDevice device, ANARI_EXT_debug_interface *iface) {
    int ok = 1;
    ok = ok && (iface->anariInsertStatusMessage = (PFNANARIINSERTSTATUSMESSAGE)anariDeviceGetProcAddress(device, "anariInsertStatusMessage"));
    ok = ok && (iface->anariNameObject = (PFNANARINAMEOBJECT)anariDeviceGetProcAddress(device, "anariNameObject"));
    return ok;
}

#define anariInsertStatusMessage ANARI_EXT_debug_interface_impl.anariInsertStatusMessage
#define anariNameObject ANARI_EXT_debug_interface_impl.anariNameObject

#ifdef ANARI_EXT_IMPLEMENTATION
ANARI_EXT_debug_interface ANARI_EXT_debug_interface_impl;
#else
extern ANARI_EXT_debug_interface ANARI_EXT_debug_interface_impl;
#endif

static inline int load_ANARI_EXT_debug_interface(ANARIDevice device) {
    return init_ANARI_EXT_debug_interface(device, &ANARI_EXT_debug_interface_impl);
}

#ifdef __cplusplus
} // extern "C"
#endif