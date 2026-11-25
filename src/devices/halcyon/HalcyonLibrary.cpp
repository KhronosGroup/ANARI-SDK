// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "HalcyonDevice.h"
#include "anari/backend/LibraryImpl.h"
#include "anari/ext/halcyon/anariNewHalcyonDevice.h"
#include "anari_library_halcyon_queries.h"

namespace halcyon {

struct HalcyonLibrary : public anari::LibraryImpl
{
  HalcyonLibrary(
      void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr);

  ANARIDevice newDevice(const char *subtype) override;
  const char **getDeviceExtensions(const char *deviceType) override;
};

// Definitions ////////////////////////////////////////////////////////////////

HalcyonLibrary::HalcyonLibrary(
    void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr)
    : anari::LibraryImpl(lib, defaultStatusCB, statusCBPtr)
{}

ANARIDevice HalcyonLibrary::newDevice(const char * /*subtype*/)
{
  return (ANARIDevice) new HalcyonDevice(this_library());
}

const char **HalcyonLibrary::getDeviceExtensions(const char * /*deviceType*/)
{
  return query_extensions();
}

} // namespace halcyon

// Define library entrypoint //////////////////////////////////////////////////

extern "C" HALCYON_EXPORT ANARI_DEFINE_LIBRARY_ENTRYPOINT(
    halcyon, handle, scb, scbPtr)
{
  return (ANARILibrary) new halcyon::HalcyonLibrary(handle, scb, scbPtr);
}

extern "C" HALCYON_EXPORT ANARIDevice anariNewHalcyonDevice(
    ANARIStatusCallback defaultCallback, const void *userPtr)
{
  return (ANARIDevice) new halcyon::HalcyonDevice(defaultCallback, userPtr);
}
