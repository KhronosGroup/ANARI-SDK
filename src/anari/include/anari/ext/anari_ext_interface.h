#pragma once

#include <anari/anari.h>

#ifdef __cplusplus
extern "C" {
#endif

// query extension function pointers
ANARI_INTERFACE void (* anariDeviceGetProcAddress(ANARIDevice, const char *name))(void);

#ifdef __cplusplus
} // extern "C"
#endif
