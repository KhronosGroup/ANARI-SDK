// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/BaseDevice.h"

#include "CaptureState.h"

namespace capture_device {

struct CaptureDevice : public helium::BaseDevice
{
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
  ANARIMaterial newMaterial(const char *type) override;
  ANARIRenderer newRenderer(const char *type) override;
  ANARISampler newSampler(const char *type) override;
  ANARISpatialField newSpatialField(const char *type) override;
  ANARISurface newSurface() override;
  ANARIVolume newVolume(const char *type) override;
  ANARIWorld newWorld() override;
  ANARIObject newObject(const char *objectType, const char *type) override;

  // The rest of the API, counted and passed on to helium ////////////////////

  void *mapArray(ANARIArray) override;
  void unmapArray(ANARIArray) override;
  int getProperty(ANARIObject o,
      const char *name,
      ANARIDataType type,
      void *mem,
      uint64_t size,
      uint32_t mask) override;
  void setParameter(ANARIObject o,
      const char *name,
      ANARIDataType type,
      const void *mem) override;
  void unsetParameter(ANARIObject o, const char *name) override;
  void unsetAllParameters(ANARIObject o) override;
  void *mapParameterArray1D(ANARIObject o,
      const char *name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t *elementStride) override;
  void *mapParameterArray2D(ANARIObject o,
      const char *name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t numElements2,
      uint64_t *elementStride) override;
  void *mapParameterArray3D(ANARIObject o,
      const char *name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t numElements2,
      uint64_t numElements3,
      uint64_t *elementStride) override;
  void unmapParameterArray(ANARIObject o, const char *name) override;
  void commitParameters(ANARIObject o) override;
  void release(ANARIObject o) override;
  void retain(ANARIObject o) override;
  const void *frameBufferMap(ANARIFrame f,
      const char *channel,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType) override;
  void frameBufferUnmap(ANARIFrame f, const char *channel) override;
  void renderFrame(ANARIFrame f) override;
  int frameReady(ANARIFrame f, ANARIWaitMask m) override;
  void discardFrame(ANARIFrame f) override;
  void (*getProcAddress(const char *name))(void) override;

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

  // Other ////////////////////////////////////////////////////////////////////

  CaptureDevice(ANARILibrary l);
  ~CaptureDevice() override;

  void deviceCommitParameters() override;
  int deviceGetProperty(const char *name,
      ANARIDataType type,
      void *mem,
      uint64_t size,
      uint32_t mask) override;

 private:
  CaptureState *deviceState() const;
  // Counts a call from the application. Calls helium makes to the device on
  // the application's behalf (mapParameterArray*() creates, sets and maps an
  // array) are not counted.
  void count(ApiCall call);
};

} // namespace capture_device
