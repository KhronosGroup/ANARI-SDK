// Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "DebugDevice.h"
#include "anari/backend/LibraryImpl.h"

#include "anari/ext/debug/debug_device_exports.h"

namespace anari {
namespace debug_device {

struct DebugLibrary : public anari::LibraryImpl
{
  DebugLibrary(
      void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr);

  ANARIDevice newDevice(const char *subtype) override;
  const char **getDeviceExtensions(const char *deviceType) override;
};

// Definitions ////////////////////////////////////////////////////////////////

DebugLibrary::DebugLibrary(
    void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr)
    : anari::LibraryImpl(lib, defaultStatusCB, statusCBPtr)
{}

ANARIDevice DebugLibrary::newDevice(const char * /*subtype*/)
{
  return (ANARIDevice) new DebugDevice(this_library());
}

const char **DebugLibrary::getDeviceExtensions(const char * /*deviceType*/)
{
  return nullptr;
}

} // namespace debug_device
} // namespace anari

// Define library entrypoint //////////////////////////////////////////////////

extern "C" DEBUG_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_ENTRYPOINT(
    debug, handle, scb, scbPtr)
{
  return (ANARILibrary) new anari::debug_device::DebugLibrary(
      handle, scb, scbPtr);
}
