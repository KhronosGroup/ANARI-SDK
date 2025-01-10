// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Device.h"
#include "anari/backend/LibraryImpl.h"

#ifdef _WIN32
#ifdef REMOTE_DEVICE_STATIC_DEFINE
#define REMOTE_LIBRARY_INTERFACE
#else
#ifdef anari_library_remote_EXPORTS
#define REMOTE_LIBRARY_INTERFACE __declspec(dllexport)
#else
#define REMOTE_LIBRARY_INTERFACE __declspec(dllimport)
#endif
#endif
#elif defined __GNUC__
#define REMOTE_LIBRARY_INTERFACE __attribute__((__visibility__("default")))
#else
#define REMOTE_LIBRARY_INTERFACE
#endif

namespace remote {

struct RemoteLibrary : public anari::LibraryImpl
{
  RemoteLibrary(
      void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr);

  ANARIDevice newDevice(const char *subtype) override;
  const char **getDeviceExtensions(const char *deviceType) override;
};

// Definitions ////////////////////////////////////////////////////////////////

RemoteLibrary::RemoteLibrary(
    void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr)
    : anari::LibraryImpl(lib, defaultStatusCB, statusCBPtr)
{}

ANARIDevice RemoteLibrary::newDevice(const char * /*subtype*/)
{
  return (ANARIDevice) new Device();
}

const char **RemoteLibrary::getDeviceExtensions(const char * /*deviceType*/)
{
  return nullptr;
}

} // namespace remote

// Define library entrypoint //////////////////////////////////////////////////

extern "C" REMOTE_LIBRARY_INTERFACE ANARI_DEFINE_LIBRARY_ENTRYPOINT(remote, handle, scb, scbPtr)
{
  return (ANARILibrary) new remote::RemoteLibrary(handle, scb, scbPtr);
}
