// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/backend/utilities/IntrusivePtr.h"
// anari
#include "anari/backend/DeviceImpl.h"
#include "anari/backend/utilities/ParameterizedObject.h"
// std
#include <vector>

#include "ExampleDeviceInterface.h"

namespace anari {
namespace example_device {

struct Object;

struct EXAMPLE_DEVICE_INTERFACE ExampleDevice : public DeviceImpl,
                                                public RefCounted,
                                                public ParameterizedObject
{
  /////////////////////////////////////////////////////////////////////////////
  // Main interface to accepting API calls
  /////////////////////////////////////////////////////////////////////////////

  // Data Arrays //////////////////////////////////////////////////////////////

  ANARIArray1D newArray1D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t byteStride1) override;

  ANARIArray2D newArray2D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t byteStride1,
      uint64_t byteStride2) override;

  ANARIArray3D newArray3D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t numItems3,
      uint64_t byteStride1,
      uint64_t byteStride2,
      uint64_t byteStride3) override;

  void *mapArray(ANARIArray) override;
  void unmapArray(ANARIArray) override;

  // Renderable Objects ///////////////////////////////////////////////////////

  ANARILight newLight(const char *type) override;

  ANARICamera newCamera(const char *type) override;

  ANARIGeometry newGeometry(const char *type) override;
  ANARISpatialField newSpatialField(const char *type) override;

  ANARISurface newSurface() override;
  ANARIVolume newVolume(const char *type) override;

  // Surface Meta-Data ////////////////////////////////////////////////////////

  ANARIMaterial newMaterial(const char *material_type) override;

  ANARISampler newSampler(const char *type) override;

  // Instancing ///////////////////////////////////////////////////////////////

  ANARIGroup newGroup() override;

  ANARIInstance newInstance() override;

  // Top-level Worlds /////////////////////////////////////////////////////////

  ANARIWorld newWorld() override;

  // Object + Parameter Lifetime Management ///////////////////////////////////

  int getProperty(ANARIObject object,
      const char *name,
      ANARIDataType type,
      void *mem,
      uint64_t size,
      ANARIWaitMask mask) override;

  void setParameter(ANARIObject object,
      const char *name,
      ANARIDataType type,
      const void *mem) override;

  void unsetParameter(ANARIObject object, const char *name) override;

  void commitParameters(ANARIObject object) override;

  void release(ANARIObject _obj) override;
  void retain(ANARIObject _obj) override;

  // FrameBuffer Manipulation /////////////////////////////////////////////////

  ANARIFrame newFrame() override;

  const void *frameBufferMap(ANARIFrame fb,
      const char *channel,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType) override;

  void frameBufferUnmap(ANARIFrame fb, const char *channel) override;

  // Frame Rendering //////////////////////////////////////////////////////////

  ANARIRenderer newRenderer(const char *type) override;

  void renderFrame(ANARIFrame) override;
  int frameReady(ANARIFrame, ANARIWaitMask) override;
  void discardFrame(ANARIFrame) override;

  /////////////////////////////////////////////////////////////////////////////
  // Helper/other functions and data members
  /////////////////////////////////////////////////////////////////////////////

  ExampleDevice();
  ExampleDevice(ANARILibrary);
  ~ExampleDevice() override = default;

  void flushCommitBuffer();

 private:
  void deviceSetParameter(const char *id, ANARIDataType type, const void *mem);
  void deviceUnsetParameter(const char *id);
  void deviceCommit();

  int m_numThreads{1};
  std::vector<Object *> m_objectsToCommit;
  bool m_needToSortCommits{false};
};

} // namespace example_device
} // namespace anari
