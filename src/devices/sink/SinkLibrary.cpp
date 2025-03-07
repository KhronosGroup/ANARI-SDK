// Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SinkDevice.h"
#include "anari/backend/LibraryImpl.h"

#ifdef _WIN32
#ifdef SINK_DEVICE_STATIC_DEFINE
#define SINK_LIBRARY_INTERFACE
#else
#ifdef anari_library_sink_EXPORTS
#define SINK_LIBRARY_INTERFACE __declspec(dllexport)
#else
#define SINK_LIBRARY_INTERFACE __declspec(dllimport)
#endif
#endif
#elif defined __GNUC__
#define SINK_LIBRARY_INTERFACE __attribute__((__visibility__("default")))
#else
#define SINK_LIBRARY_INTERFACE
#endif

namespace sink_device {

struct SinkLibrary : public anari::LibraryImpl
{
  SinkLibrary(
      void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr);

  ANARIDevice newDevice(const char *subtype) override;
  const char **getDeviceExtensions(const char *deviceType) override;
};

// Definitions ////////////////////////////////////////////////////////////////

SinkLibrary::SinkLibrary(
    void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr)
    : anari::LibraryImpl(lib, defaultStatusCB, statusCBPtr)
{}

ANARIDevice SinkLibrary::newDevice(const char * /*subtype*/)
{
  return (ANARIDevice) new SinkDevice(this_library());
}

const char **SinkLibrary::getDeviceExtensions(const char * /*deviceType*/)
{
  return nullptr;
}

} // namespace sink_device

// Define library entrypoint //////////////////////////////////////////////////

extern "C" SINK_LIBRARY_INTERFACE ANARI_DEFINE_LIBRARY_ENTRYPOINT(
    sink, handle, scb, scbPtr)
{
  return (ANARILibrary) new sink_device::SinkLibrary(handle, scb, scbPtr);
}
