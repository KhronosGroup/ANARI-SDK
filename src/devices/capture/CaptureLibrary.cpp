// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "CaptureDevice.h"
#include "anari/backend/LibraryImpl.h"

#include "anari_library_capture_queries.h"

#ifdef _WIN32
#ifdef anari_library_capture_EXPORTS
#define CAPTURE_LIBRARY_INTERFACE __declspec(dllexport)
#else
#define CAPTURE_LIBRARY_INTERFACE __declspec(dllimport)
#endif
#elif defined __GNUC__
#define CAPTURE_LIBRARY_INTERFACE __attribute__((__visibility__("default")))
#else
#define CAPTURE_LIBRARY_INTERFACE
#endif

namespace capture_device {

struct CaptureLibrary : public anari::LibraryImpl
{
  CaptureLibrary(
      void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr);

  ANARIDevice newDevice(const char *subtype) override;
  const char **getDeviceExtensions(const char *deviceType) override;
};

// Definitions ////////////////////////////////////////////////////////////////

CaptureLibrary::CaptureLibrary(
    void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr)
    : anari::LibraryImpl(lib, defaultStatusCB, statusCBPtr)
{}

ANARIDevice CaptureLibrary::newDevice(const char * /*subtype*/)
{
  return (ANARIDevice) new CaptureDevice(this_library());
}

const char **CaptureLibrary::getDeviceExtensions(const char * /*deviceType*/)
{
  return query_extensions();
}

} // namespace capture_device

// Define library entrypoint //////////////////////////////////////////////////

extern "C" CAPTURE_LIBRARY_INTERFACE ANARI_DEFINE_LIBRARY_ENTRYPOINT(
    capture, handle, scb, scbPtr)
{
  return (ANARILibrary) new capture_device::CaptureLibrary(handle, scb, scbPtr);
}
