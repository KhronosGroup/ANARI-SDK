// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include "anari/anari.h"
#include "anari/anari_cpp/Traits.h"
// std
#include <string>

namespace anari {

// Function prototypes for misc. library functions needed by API.cpp
void *loadANARILibrary(const std::string &libName);
void freeLibrary(void *lib);
void *getSymbolAddress(void *lib, const std::string &symbol);

struct LibraryImpl
{
  LibraryImpl(
      void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr);
  virtual ~LibraryImpl();

  // Create an instance of anari::DeviceImpl, typically using the 'new' operator
  virtual ANARIDevice newDevice(const char *subtype) = 0;
  // Implement anariGetDevcieExtensions()
  virtual const char **getDeviceExtensions(const char *deviceType) = 0;
  // Optionally implement anariGetDeviceSubtypes(), get {"default", 0} otherwise
  virtual const char **getDeviceSubtypes();

  // Optionally implement anariLoadModule()
  virtual void loadModule(const char *name);
  // Optionally implement anariUnloadModule()
  virtual void unloadModule(const char *name);

  // Get the default callback passed to anariLoadLibrary()
  ANARIStatusCallback defaultStatusCB() const;
  // Get the default callback user pointer passed to anariLoadLibrary()
  const void *defaultStatusCBUserPtr() const;

  // Utility to get 'this' pointer as an ANARILibrary handle
  ANARILibrary this_library() const;

 private:
  void *m_lib{nullptr};

  ANARIStatusCallback m_defaultStatusCB{nullptr};
  const void *m_defaultStatusCBUserPtr{nullptr};
};

// [REQUIRED] Define the function which allocates Device objects by subtype
#define ANARI_DEFINE_LIBRARY_ENTRYPOINT(libname, handle, scb, scbPtr)          \
  ANARILibrary anari_library_##libname##_new_library(                          \
      void *handle, ANARIStatusCallback scb, const void *scbPtr)

ANARI_TYPEFOR_SPECIALIZATION(LibraryImpl *, ANARI_LIBRARY);

} // namespace anari
