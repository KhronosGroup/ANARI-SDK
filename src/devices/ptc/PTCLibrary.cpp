// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// ptc
#include "MpiExtension.h"
#include "PTCDevice.h"
#include "WrappedLibrary.h"
#include "anari_library_ptc_export.h"
// anari
#include "anari/backend/LibraryImpl.h"
// std
#include <exception>
#include <memory>
#include <mutex>
#include <string>

namespace anari_ptc {

struct PTCLibrary : public anari::LibraryImpl
{
  PTCLibrary(
      void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr);

  ANARIDevice newDevice(const char *subtype) override;
  const char **getDeviceExtensions(const char *deviceType) override;

 private:
  void report(ANARIStatusSeverity severity, const std::string &msg) const;
  void reportException(const char *entryPoint) const noexcept;

  std::mutex m_extensionsMutex;
  std::unique_ptr<ExtensionList> m_extensions; // built on first query
};

// Definitions ////////////////////////////////////////////////////////////////

PTCLibrary::PTCLibrary(
    void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr)
    : anari::LibraryImpl(lib, defaultStatusCB, statusCBPtr)
{}

void PTCLibrary::report(
    ANARIStatusSeverity severity, const std::string &msg) const
{
  if (auto cb = defaultStatusCB()) {
    cb(defaultStatusCBUserPtr(),
        nullptr,
        nullptr,
        ANARI_LIBRARY,
        severity,
        severity <= ANARI_SEVERITY_ERROR ? ANARI_STATUS_UNKNOWN_ERROR
                                         : ANARI_STATUS_NO_ERROR,
        msg.c_str());
  }
}

// No exception may escape through the C API: report it as an ERROR through
// the library's status callback instead. Only call from a catch block.
void PTCLibrary::reportException(const char *entryPoint) const noexcept
{
  try {
    std::string msg = entryPoint;
    try {
      throw;
    } catch (const std::exception &e) {
      msg = msg + ": " + e.what();
    } catch (...) {
      msg += ": unknown exception";
    }
    report(ANARI_SEVERITY_ERROR, msg);
  } catch (...) {
    // reporting failed as well; there is nothing left to tell the app with
  }
}

ANARIDevice PTCLibrary::newDevice(const char * /*subtype*/)
{
  try {
    return (ANARIDevice) new PTCDevice(this_library());
  } catch (...) {
    reportException("anariNewDevice");
    return nullptr;
  }
}

// The wrapped library's extensions for the device ptc wraps ('default'), plus
// the ones ptc implements itself. Every ptc device subtype is the same device.
// There is no device here, so the wrapped library is PTC_ANARI_LIBRARY or
// helide, never a device's 'wrappedLibrary' parameter.
const char **PTCLibrary::getDeviceExtensions(const char * /*deviceType*/)
{
  try {
    std::lock_guard<std::mutex> lock(m_extensionsMutex);
    if (!m_extensions) {
      const char *const *wrapped = nullptr;
      auto w = loadWrappedLibrary(
          std::string(), defaultStatusCB(), defaultStatusCBUserPtr());
      if (!w.fallbackWarning.empty())
        report(ANARI_SEVERITY_WARNING, w.fallbackWarning);
      if (w.library)
        wrapped = anariGetDeviceExtensions(w.library, "default");
      else {
        // not fatal: a device may still load another library through its
        // 'wrappedLibrary' parameter
        report(ANARI_SEVERITY_WARNING,
            w.failure + "; the extension list has only ptc's own extensions");
      }
      m_extensions = std::make_unique<ExtensionList>(wrapped); // copies names
      if (w.library)
        anariUnloadLibrary(w.library);
    }
    return m_extensions->data();
  } catch (...) {
    reportException("anariGetDeviceExtensions");
    return nullptr;
  }
}

} // namespace anari_ptc

// Define library entrypoint //////////////////////////////////////////////////

extern "C" PTC_EXPORT ANARI_DEFINE_LIBRARY_ENTRYPOINT(ptc, handle, scb, scbPtr)
{
  try {
    return (ANARILibrary) new anari_ptc::PTCLibrary(handle, scb, scbPtr);
  } catch (...) {
    return nullptr; // no library yet to report through; anari reports it
  }
}
