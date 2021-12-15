// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "anari/detail/Library.h"

// std
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/times.h>
#endif

#include <stdexcept>
#include <vector>

#if defined(__MACOSX__) || defined(__APPLE__)
#define ANARI_LIB_EXT ".dylib"
#else
#define ANARI_LIB_EXT ".so"
#endif

#ifdef _WIN32
#define ANARI_LOOKUP_SYM(lib, symbol) GetProcAddress((HMODULE)lib, symbol);
#define ANARI_FREE_LIB(lib) FreeLibrary((HMODULE)lib);
#else
#define ANARI_LOOKUP_SYM(lib, symbol) dlsym(lib, symbol);
#define ANARI_FREE_LIB(lib) dlclose(lib)
#endif

/* Export a symbol to ask the dynamic loader about in order to locate this
 * library at runtime. */
extern "C" int _anari_anchor()
{
  return 0;
}

namespace {

std::string library_location()
{
#if defined(_WIN32) && !defined(__CYGWIN__)
  MEMORY_BASIC_INFORMATION mbi;
  VirtualQuery(&_anari_anchor, &mbi, sizeof(mbi));
  char pathBuf[16384];
  if (!GetModuleFileNameA(
          static_cast<HMODULE>(mbi.AllocationBase), pathBuf, sizeof(pathBuf)))
    return std::string();

  std::string path = std::string(pathBuf);
  path.resize(path.rfind('\\') + 1);
#else
  const char *anchor = "_anari_anchor";
  void *handle = dlsym(RTLD_DEFAULT, anchor);
  if (!handle)
    return std::string();

  Dl_info di;
  int ret = dladdr(handle, &di);
  if (!ret || !di.dli_saddr || !di.dli_fname)
    return std::string();

  std::string path = std::string(di.dli_fname);
  path.resize(path.rfind('/') + 1);
#endif

  return path;
}

} // namespace

namespace anari {

using InitFcn = void (*)();
using AllocFcn = void *(*)();

static std::vector<void *> g_loadedLibs;

static void *loadLibrary(
    const std::string &libName, bool withAnchor, std::string &errorMessage)
{
  std::string file = libName;
  std::string errorMsg;
  std::string libLocation = withAnchor ? library_location() : std::string();
  void *lib = nullptr;
#ifdef _WIN32
  std::string fullName = libLocation + file + ".dll";
  lib = LoadLibrary(fullName.c_str());
  if (lib == nullptr) {
    DWORD err = GetLastError();
    LPTSTR lpMsgBuf;
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf,
        0,
        NULL);

    errorMsg = lpMsgBuf;

    LocalFree(lpMsgBuf);
  }
#else
  std::string fullName = libLocation + "lib" + file + ANARI_LIB_EXT;
  lib = dlopen(fullName.c_str(), RTLD_LAZY | RTLD_LOCAL);
  if (lib == nullptr) {
    errorMsg += dlerror();
  }
#endif

  if (lib == nullptr) {
    errorMessage += " could not open library lib " + libName + ": " + errorMsg;
  }

  return lib;
}

void *loadANARILibrary(const std::string &libName)
{
  std::string errorMessage;

  void *lib = loadLibrary(libName, false, errorMessage);
  if (!lib) {
    errorMessage = "(unanchored library load attempt failed)\n";
    lib = loadLibrary(libName, true, errorMessage);
  }

  if (!lib)
    throw std::runtime_error(errorMessage);

  return lib;
}

void *getSymbolAddress(void *lib, const std::string &symbol)
{
  return ANARI_LOOKUP_SYM(lib, symbol.c_str());
}

void freeLibrary(void *lib)
{
  if (lib)
    ANARI_FREE_LIB(lib);
}

Library::Library(
    std::string name, ANARIStatusCallback defaultStatusCB, void *statusCBPtr)
{
  void *lib = nullptr;
  if (name == "debug")
    lib = loadANARILibrary("anari_debug");
  else
    lib = loadANARILibrary(std::string("anari_library_") + name);

  if (!lib)
    throw std::runtime_error("failed to load library " + std::string(name));

  m_handle = lib;

  std::string initFcnName = "anaribackend_load_library_" + name;
  auto initFcn =
      (ANARIBackendLoadLibrary *)getSymbolAddress(m_handle, initFcnName);

  if (!initFcn)
    throw std::runtime_error(
        "failed to find load_library function for " + name + " library");

  m_lib = initFcn(defaultStatusCB, statusCBPtr);

  if (!m_lib)
    throw std::runtime_error("failed to initialize " + name + " library");

  auto devices = getDeviceSubtypes();
  if (devices && devices[0])
    m_defaultDeviceName = devices[0];
}

Library::~Library()
{
  if (m_lib->unloadLibrary)
    m_lib->unloadLibrary(m_lib->data);

  freeLibrary(m_handle);
}

void Library::loadModule(const char *name) const
{
  // TODO
}

void Library::unloadModule(const char *name) const
{
  // TODO
}

const char *Library::replaceDefaultDeviceName(const char *deviceSubtype) const
{
  if (std::string(deviceSubtype) == "default")
    return m_defaultDeviceName.c_str();
  return deviceSubtype;
}

const char **Library::getDeviceSubtypes()
{
  if (m_lib->getDeviceSubtypes)
    return m_lib->getDeviceSubtypes(m_lib->data);

  return nullptr;
}

const char **Library::getObjectSubtypes(
    const char *deviceSubtype, ANARIDataType objectType)
{
  if (m_lib->getObjectSubtypes)
    return m_lib->getObjectSubtypes(
        m_lib->data, replaceDefaultDeviceName(deviceSubtype), objectType);
  return nullptr;
}

const ANARIParameter *Library::getObjectParameters(const char *deviceSubtype,
    const char *objectSubtype,
    ANARIDataType objectType)
{
  if (m_lib->getObjectParameters)
    return m_lib->getObjectParameters(m_lib->data,
        replaceDefaultDeviceName(deviceSubtype),
        objectSubtype,
        objectType);
  return nullptr;
}

const void *Library::getParameterInfo(const char *deviceSubtype,
    const char *objectSubtype,
    ANARIDataType objectType,
    const char *parameterName,
    ANARIDataType parameterType,
    const char *infoName,
    ANARIDataType infoType)
{
  if (m_lib->getParameterInfo)
    return m_lib->getParameterInfo(m_lib->data,
        replaceDefaultDeviceName(deviceSubtype),
        objectSubtype,
        objectType,
        parameterName,
        parameterType,
        infoName,
        infoType);
  return nullptr;
}

ANARIDevice Library::newDevice(const char *subtype) const
{
  if (m_lib->newDevice)
    return m_lib->newDevice(m_lib->data, replaceDefaultDeviceName(subtype));
  return nullptr;
}

} // namespace anari
