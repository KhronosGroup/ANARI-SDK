// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "HelideGPUDevice.h"
#include "anari/backend/LibraryImpl.h"
#include "anari_library_helide_gpu_export.h"
#include "anari_library_helide_gpu_queries.h"

namespace helide_gpu {

struct HelideGPULibrary : public anari::LibraryImpl
{
  HelideGPULibrary(
      void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr);

  ANARIDevice newDevice(const char *subtype) override;
  const char **getDeviceExtensions(const char *deviceType) override;
};

// Definitions ////////////////////////////////////////////////////////////////

HelideGPULibrary::HelideGPULibrary(
    void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr)
    : anari::LibraryImpl(lib, defaultStatusCB, statusCBPtr)
{}

ANARIDevice HelideGPULibrary::newDevice(const char * /*subtype*/)
{
  return (ANARIDevice) new HelideGPUDevice(this_library());
}

const char **HelideGPULibrary::getDeviceExtensions(const char * /*deviceType*/)
{
  return query_extensions();
}

} // namespace helide_gpu

// Define library entrypoint //////////////////////////////////////////////////

extern "C" HELIDE_GPU_EXPORT ANARI_DEFINE_LIBRARY_ENTRYPOINT(
    helide_gpu, handle, scb, scbPtr)
{
  return (ANARILibrary) new helide_gpu::HelideGPULibrary(handle, scb, scbPtr);
}
