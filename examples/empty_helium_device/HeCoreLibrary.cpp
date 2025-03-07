// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "HeCoreDevice.h"
#include "anari/backend/LibraryImpl.h"
#include "anari_library_hecore_export.h"

namespace hecore {

struct HeCoreLibrary : public anari::LibraryImpl
{
  HeCoreLibrary(
      void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr);

  ANARIDevice newDevice(const char *subtype) override;
  const char **getDeviceExtensions(const char *deviceType) override;
};

// Definitions ////////////////////////////////////////////////////////////////

HeCoreLibrary::HeCoreLibrary(
    void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr)
    : anari::LibraryImpl(lib, defaultStatusCB, statusCBPtr)
{}

ANARIDevice HeCoreLibrary::newDevice(const char * /*subtype*/)
{
  return (ANARIDevice) new HeCoreDevice(this_library());
}

const char **HeCoreLibrary::getDeviceExtensions(const char * /*deviceType*/)
{
  return nullptr;
}

} // namespace hecore

// Define library entrypoint //////////////////////////////////////////////////

extern "C" HECORE_EXPORT ANARI_DEFINE_LIBRARY_ENTRYPOINT(
    hecore, handle, scb, scbPtr)
{
  return (ANARILibrary) new hecore::HeCoreLibrary(handle, scb, scbPtr);
}
