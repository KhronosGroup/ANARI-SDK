// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "adapter/GuardDevice.h"
#include "adapter/OutOfScope.h"
#include "anari_library_guard_export.h"

// anari
#include <anari/backend/LibraryImpl.h>
// std
#include <map>
#include <mutex>

namespace anari_guard {

struct GuardLibrary : public anari::LibraryImpl
{
  GuardLibrary(
      void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr)
      : anari::LibraryImpl(lib, defaultStatusCB, statusCBPtr)
  {}

  // The module may be unmapped once this returns.
  ~GuardLibrary() override
  {
    GuardDevice::waitForReleasesInCallbacks();
  }

  ANARIDevice newDevice(const char *subtype) override;
  const char **getDeviceExtensions(const char *deviceType) override;

 private:
  void report(ANARIStatusSeverity severity, const std::string &message) const;
  bool queryExtensions(
      const WrappedDevice &wrapped, Interned &out, std::string &error);

  std::mutex m_extensionsMutex;
  std::map<std::string, Interned> m_extensions; // by wrapped device identifier
};

void GuardLibrary::report(
    ANARIStatusSeverity severity, const std::string &message) const
{
  if (auto cb = defaultStatusCB()) {
    cb(defaultStatusCBUserPtr(),
        nullptr,
        nullptr,
        ANARI_LIBRARY,
        severity,
        severity <= ANARI_SEVERITY_ERROR ? ANARI_STATUS_UNKNOWN_ERROR
                                         : ANARI_STATUS_NO_ERROR,
        message.c_str());
  }
}

ANARIDevice GuardLibrary::newDevice(const char *subtype)
{
  WrappedDevice wrapped;
  std::string error;
  GuardDevice *device = nullptr;
  if (resolveWrappedDevice(subtype, wrapped, error))
    device = GuardDevice::create(this_library(), wrapped, error);
  if (!device)
    report(ANARI_SEVERITY_ERROR, "guard device: " + error);
  return ANARIDevice(device);
}

// Ask a short-lived host, which loads the wrapped library, answers and exits.
bool GuardLibrary::queryExtensions(
    const WrappedDevice &wrapped, Interned &out, std::string &error)
{
  HostConnection host;
  if (!host.start(error))
    return false;

  Buffer request;
  {
    Writer w(request, Op::QUERY_EXTENSIONS);
    w.string(wrapped.library);
    w.string(wrapped.subtype);
  }

  MessageHeader header;
  Buffer reply;
  bool ok = platform::sendAll(host.call, request.data(), request.size())
      && recvMessage(host.call, header, reply);
  if (!ok)
    error = "the host ended while answering an extension query";
  else {
    Reader r(reply);
    ok = r.pod<uint8_t>() != 0;
    std::string failure = r.string();
    if (!ok)
      error = failure;
    else {
      uint32_t count = r.pod<uint32_t>();
      for (uint32_t i = 0; i < count && r.ok(); i++)
        out.strings.push_back(r.string());
    }
  }

  platform::reapHost(host.process);
  host.closeEndpoints();
  return ok;
}

const char **GuardLibrary::getDeviceExtensions(const char *deviceType)
{
  static const char *none[] = {nullptr};

  WrappedDevice wrapped;
  std::string error;
  if (!resolveWrappedDevice(deviceType, wrapped, error)) {
    report(ANARI_SEVERITY_WARNING, "guard device: " + error);
    return none;
  }

  std::lock_guard<std::mutex> lock(m_extensionsMutex);
  const std::string key = wrapped.subtype + '@' + wrapped.library;
  auto it = m_extensions.find(key);
  if (it == m_extensions.end()) {
    Interned list;
    if (!queryExtensions(wrapped, list, error)) {
      report(ANARI_SEVERITY_WARNING, "guard device: " + error);
      return none;
    }
    removeOutOfScopeExtensions(list.strings);
    it = m_extensions.emplace(key, std::move(list)).first;
    for (auto &s : it->second.strings)
      it->second.pointers.push_back(s.c_str());
    it->second.pointers.push_back(nullptr);
  }
  return it->second.pointers.data();
}

} // namespace anari_guard

extern "C" GUARD_EXPORT ANARI_DEFINE_LIBRARY_ENTRYPOINT(
    guard, handle, scb, scbPtr)
{
  return (ANARILibrary) new anari_guard::GuardLibrary(handle, scb, scbPtr);
}
