// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "HelideDevice.h"
#include "anari/backend/LibraryImpl.h"
#include "anari_library_helide_export.h"

namespace helide {

const char **query_extensions();

struct HelideLibrary : public anari::LibraryImpl
{
  HelideLibrary(
      void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr);

  ANARIDevice newDevice(const char *subtype) override;
  const char **getDeviceExtensions(const char *deviceType) override;
};

// Definitions ////////////////////////////////////////////////////////////////

HelideLibrary::HelideLibrary(
    void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr)
    : anari::LibraryImpl(lib, defaultStatusCB, statusCBPtr)
{}

ANARIDevice HelideLibrary::newDevice(const char * /*subtype*/)
{
  return (ANARIDevice) new HelideDevice(this_library());
}

const char **HelideLibrary::getDeviceExtensions(const char * /*deviceType*/)
{
  return query_extensions();
}

} // namespace helide

// Define library entrypoint //////////////////////////////////////////////////

extern "C" HELIDE_EXPORT ANARI_DEFINE_LIBRARY_ENTRYPOINT(
    helide, handle, scb, scbPtr)
{
  return (ANARILibrary) new helide::HelideLibrary(handle, scb, scbPtr);
}

extern "C" HELIDE_EXPORT ANARIDevice anariNewHelideDevice(
    ANARIStatusCallback defaultCallback, const void *userPtr)
{
  return (ANARIDevice) new helide::HelideDevice(defaultCallback, userPtr);
}
