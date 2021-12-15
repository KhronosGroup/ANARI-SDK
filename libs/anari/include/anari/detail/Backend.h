// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari.h"

struct ANARIBackendLibraryData;
typedef ANARIBackendLibraryData *(
    ANARIBackendLoadLibrary)(ANARIStatusCallback defaultStatusCB,
    void *defaultStatusCBUserPtr);
typedef void ANARIBackendLibrary;
typedef void (*ANARIBackendUnloadLibrary)(ANARIBackendLibrary *);
typedef const char **(*ANARIBackendGetDeviceSubtypes)(ANARIBackendLibrary *);
typedef const char **(*ANARIBackendGetObjectSubtypes)(
    ANARIBackendLibrary *, const char *deviceSubtype, ANARIDataType objectType);
typedef const ANARIParameter *(*ANARIBackendGetObjectParameters)(
    ANARIBackendLibrary *,
    const char *deviceSubtype,
    const char *objectSubtype,
    ANARIDataType objectType);
typedef const void *(*ANARIBackendGetParameterInfo)(ANARIBackendLibrary *,
    const char *deviceSubtype,
    const char *objectSubtype,
    ANARIDataType objectType,
    const char *parameterName,
    ANARIDataType parameterType,
    const char *infoName,
    ANARIDataType infoType);
struct ANARIBackendDevice;
// typedef ANARIBackendDevice *(*ANARIBackendNewDevice)(
typedef ANARIDevice (*ANARIBackendNewDevice)(
    ANARIBackendLibrary *, const char *deviceType);

struct ANARIBackendLibraryData
{
  ANARIBackendLibrary *data;
  ANARIBackendUnloadLibrary unloadLibrary;
  ANARIBackendGetDeviceSubtypes getDeviceSubtypes;
  ANARIBackendGetObjectSubtypes getObjectSubtypes;
  ANARIBackendGetObjectParameters getObjectParameters;
  ANARIBackendGetParameterInfo getParameterInfo;
  ANARIBackendNewDevice newDevice;
};

#define ANARIBACKEND_DEFINE_LOAD_LIBRARY(                                      \
    libName, defaultStatusCB, defaultStatusCBUserPtr)                          \
  ANARIBackendLibraryData *anaribackend_load_library_##libName(                \
      ANARIStatusCallback defaultStatusCB, void *defaultStatusCBUserPtr)

// backends may append user data
struct ANARIBackendDevice
{
  // TODO
};
