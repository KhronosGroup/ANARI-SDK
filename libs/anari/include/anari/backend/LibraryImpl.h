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
void freeLibrary(void *lib);
void *getSymbolAddress(void *lib, const std::string &symbol);

struct ANARI_INTERFACE LibraryImpl
{
  LibraryImpl(
      void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr);
  virtual ~LibraryImpl();

  virtual ANARIDevice newDevice(const char *subtype) = 0;
  virtual const char **getDeviceExtensions(const char *deviceType) = 0;
  virtual const char **getDeviceSubtypes();

  virtual void loadModule(const char *name);
  virtual void unloadModule(const char *name);

  ANARIStatusCallback defaultStatusCB() const;
  const void *defaultStatusCBUserPtr() const;

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
