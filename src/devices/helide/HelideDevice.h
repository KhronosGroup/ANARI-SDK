// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/BaseDevice.h"

#include "HelideGlobalState.h"
#include "Object.h"

namespace helide {

struct HelideDevice : public helium::BaseDevice
{
  /////////////////////////////////////////////////////////////////////////////
  // Main interface to accepting API calls
  /////////////////////////////////////////////////////////////////////////////

  // Data Arrays //////////////////////////////////////////////////////////////

  void *mapArray(ANARIArray) override;
  void unmapArray(ANARIArray) override;

  // API Objects //////////////////////////////////////////////////////////////

  ANARIArray1D newArray1D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1) override;
  ANARIArray2D newArray2D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2) override;
  ANARIArray3D newArray3D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t numItems3) override;
  ANARICamera newCamera(const char *type) override;
  ANARIFrame newFrame() override;
  ANARIGeometry newGeometry(const char *type) override;
  ANARIGroup newGroup() override;
  ANARIInstance newInstance(const char *type) override;
  ANARILight newLight(const char *type) override;
  ANARIMaterial newMaterial(const char *material_type) override;
  ANARIRenderer newRenderer(const char *type) override;
  ANARISampler newSampler(const char *type) override;
  ANARISpatialField newSpatialField(const char *type) override;
  ANARISurface newSurface() override;
  ANARIVolume newVolume(const char *type) override;
  ANARIWorld newWorld() override;

  // Query functions //////////////////////////////////////////////////////////

  const char **getObjectSubtypes(ANARIDataType objectType) override;
  const void *getObjectInfo(ANARIDataType objectType,
      const char *objectSubtype,
      const char *infoName,
      ANARIDataType infoType) override;
  const void *getParameterInfo(ANARIDataType objectType,
      const char *objectSubtype,
      const char *parameterName,
      ANARIDataType parameterType,
      const char *infoName,
      ANARIDataType infoType) override;

  /////////////////////////////////////////////////////////////////////////////
  // Helper/other functions and data members
  /////////////////////////////////////////////////////////////////////////////

  HelideDevice(ANARIStatusCallback defaultCallback, const void *userPtr);
  HelideDevice(ANARILibrary);
  ~HelideDevice() override;

  void initDevice();

  void deviceCommitParameters() override;
  int deviceGetProperty(
      const char *name, ANARIDataType type, void *mem, uint64_t size) override;

 private:
  HelideGlobalState *deviceState() const;

  bool m_initialized{false};
};

} // namespace helide
