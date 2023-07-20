// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include "anari/anari.h"
#include "anari/anari_cpp/Traits.h"
// std
#include <string>

namespace anari {

void *loadANARILibrary(const std::string &libName);
void *getSymbolAddress(void *lib, const std::string &symbol);
void freeLibrary(void *lib);

struct ANARI_INTERFACE LibraryImpl
{
  LibraryImpl(const char *name,
      ANARIStatusCallback defaultStatusCB,
      const void *statusCBPtr);
  ~LibraryImpl();

  void *libraryData() const;

  ANARIDevice newDevice(const char *subtype) const;

  const char *defaultDeviceName() const;
  ANARIStatusCallback defaultStatusCB() const;
  const void *defaultStatusCBUserPtr() const;

  void loadModule(const char *name) const;
  void unloadModule(const char *name) const;

  const char **getDeviceSubtypes();
  const char **getDeviceFeatures(const char *deviceType);

 private:
  void *m_lib{nullptr};
  void *m_libraryData{nullptr};

  std::string m_defaultDeviceName;
  ANARIStatusCallback m_defaultStatusCB{nullptr};
  const void *m_defaultStatusCBUserPtr{nullptr};

  using NewDeviceFcn = ANARIDevice (*)(ANARILibrary, const char *);
  NewDeviceFcn m_newDeviceFcn{nullptr};

  using FreeFcn = void (*)(void *);
  FreeFcn m_freeFcn{nullptr};

  using ModuleFcn = void (*)(ANARILibrary, const char *);
  ModuleFcn m_loadModuleFcn{nullptr};
  ModuleFcn m_unloadModuleFcn{nullptr};

  using GetDeviceSubtypesFcn = const char **(*)(ANARILibrary);
  GetDeviceSubtypesFcn m_getDeviceSubtypesFcn{nullptr};

  using GetDeviceFeaturesFcn = const char **(*)(ANARILibrary,
      const char*);
  GetDeviceFeaturesFcn m_getDeviceFeaturesFcn{nullptr};
};

// [REQUIRED] Define the function which allocates Device objects by subtype
#define ANARI_DEFINE_LIBRARY_NEW_DEVICE(libname, library, type)                \
  ANARIDevice anari_library_##libname##_new_device(                            \
      ANARILibrary library, const char *type)

// [OPTIONAL] Define the initialization function when library is loaded.
#define ANARI_DEFINE_LIBRARY_INIT(libname) void anari_library_##libname##_init()

// [OPTIONAL] Define the function which allocates library-specific storage on
//            construction (after init).
#define ANARI_DEFINE_LIBRARY_ALLOCATE(libname)                                 \
  void *anari_library_##libname##_allocate()

// [OPTIONAL] Define the function which frees library-specific storage on
//            destruction.
#define ANARI_DEFINE_LIBRARY_FREE(libname, library)                            \
  void anari_library_##libname##_free(ANARILibrary library)

// [OPTIONAL] Define the function which loads a library-specific module.
#define ANARI_DEFINE_LIBRARY_LOAD_MODULE(libname, library, modname)            \
  void anari_library_##libname##_load_module(                                  \
      ANARILibrary library, const char *modname)

// [OPTIONAL] Define the function which unloads a library-specific module.
#define ANARI_DEFINE_LIBRARY_UNLOAD_MODULE(libname, library, modname)          \
  void anari_library_##libname##_unload_module(                                \
      ANARILibrary library, const char *modname)

// [OPTIONAL] Define introspection functions.
#define ANARI_DEFINE_LIBRARY_GET_DEVICE_SUBTYPES(libname, library)             \
  const char **anari_library_##libname##_get_device_subtypes(                  \
      ANARILibrary library)

#define ANARI_DEFINE_LIBRARY_GET_DEVICE_FEATURES(libname, library, deviceType) \
  const char **anari_library_##libname##_get_device_features(                  \
      ANARILibrary library, const char *deviceType)

ANARI_TYPEFOR_SPECIALIZATION(LibraryImpl *, ANARI_LIBRARY);

} // namespace anari
