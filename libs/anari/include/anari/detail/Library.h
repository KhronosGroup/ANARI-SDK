// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include "anari/anari.h"
#include "anari/anari_cpp/Traits.h"
#include "anari/detail/Backend.h"

namespace anari {

void *loadANARILibrary(const std::string &libName);
void *getSymbolAddress(void *lib, const std::string &symbol);
void freeLibrary(void *lib);

struct ANARI_INTERFACE Library
{
  Library(
      std::string name, ANARIStatusCallback defaultStatusCB, void *statusCBPtr);
  ~Library();

  ANARIStatusCallback defaultStatusCB() const;
  void *defaultStatusCBUserPtr() const;

  void loadModule(const char *name) const;
  void unloadModule(const char *name) const;

  const char **getDeviceSubtypes();
  const char **getObjectSubtypes(
      const char *deviceSubtype, ANARIDataType objectType);
  const ANARIParameter *getObjectParameters(const char *deviceSubtype,
      const char *objectSubtype,
      ANARIDataType objectType);
  const void *getParameterInfo(const char *deviceSubtype,
      const char *objectSubtype,
      ANARIDataType objectType,
      const char *parameterName,
      ANARIDataType parameterType,
      const char *propertyName,
      ANARIDataType propertyType);

  ANARIDevice newDevice(const char *subtype) const;

 private:
  const char *replaceDefaultDeviceName(const char *deviceSubtype) const;
  std::string m_defaultDeviceName;
  void *m_handle{nullptr};
  ANARIBackendLibraryData *m_lib{nullptr};
};

ANARI_TYPEFOR_SPECIALIZATION(Library *, ANARI_LIBRARY);

} // namespace anari
