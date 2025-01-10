// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include "anari/anari.h"
#include "anari/anari_cpp/Traits.h"

#include "anari/backend/LibraryImpl.h"

namespace anari {

struct DeviceImpl
{
  /////////////////////////////////////////////////////////////////////////////
  // Main virtual interface to accepting API calls
  /////////////////////////////////////////////////////////////////////////////

  // Object creation //////////////////////////////////////////////////////////

  // Implement anariNewArray1D()
  virtual ANARIArray1D newArray1D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1) = 0;

  // Implement anariNewArray2D()
  virtual ANARIArray2D newArray2D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2) = 0;

  // Implement anariNewArray3D()
  virtual ANARIArray3D newArray3D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t numItems3) = 0;

  // Implement anariNewGeometry()
  virtual ANARIGeometry newGeometry(const char *type) = 0;

  // Implement anariNewMaterial()
  virtual ANARIMaterial newMaterial(const char *material_type) = 0;

  // Implement anariNewSampler()
  virtual ANARISampler newSampler(const char *type) = 0;

  // Implement anariNewSurface()
  virtual ANARISurface newSurface() = 0;

  // Implement anariNewSpatialField()
  virtual ANARISpatialField newSpatialField(const char *type) = 0;

  // Implement anariNewVolume()
  virtual ANARIVolume newVolume(const char *type) = 0;

  // Implement anariNewLight()
  virtual ANARILight newLight(const char *type) = 0;

  // Implement anariGroup()
  virtual ANARIGroup newGroup() = 0;

  // Implement anariNewInstance()
  virtual ANARIInstance newInstance(const char *type) = 0;

  // Implement anariNewWorld()
  virtual ANARIWorld newWorld() = 0;

  // Implement anariNewCamera()
  virtual ANARICamera newCamera(const char *type) = 0;

  // Implement anariNewRenderer()
  virtual ANARIRenderer newRenderer(const char *type) = 0;

  // Implement anariNewFrame()
  virtual ANARIFrame newFrame() = 0;

  // Object + Parameter Lifetime Management ///////////////////////////////////

  // Implement anariSetParameter()
  virtual void setParameter(ANARIObject object,
      const char *name,
      ANARIDataType type,
      const void *mem) = 0;

  // Implement anariUnsetParameter()
  virtual void unsetParameter(ANARIObject object, const char *name) = 0;

  // Implement anariUnsetAllParameters()
  virtual void unsetAllParameters(ANARIObject object) = 0;

  // Implement anariMapParameterArray1D()
  virtual void *mapParameterArray1D(ANARIObject object,
      const char *name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t *elementStride) = 0;

  // Implement anariMapParameterArray2D()
  virtual void *mapParameterArray2D(ANARIObject object,
      const char *name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t numElements2,
      uint64_t *elementStride) = 0;

  // Implement anariMapParameterArray3D()
  virtual void *mapParameterArray3D(ANARIObject object,
      const char *name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t numElements2,
      uint64_t numElements3,
      uint64_t *elementStride) = 0;

  // Implement anariUnmapParameterArray()
  virtual void unmapParameterArray(ANARIObject object, const char *name) = 0;

  // Implement anariCommitParameters()
  virtual void commitParameters(ANARIObject object) = 0;

  // Implement anariRelease()
  virtual void release(ANARIObject _obj) = 0;

  // Implement anariRetain()
  virtual void retain(ANARIObject _obj) = 0;

  // Implement anariMapArray()
  virtual void *mapArray(ANARIArray) = 0;

  // Implement anariUnmapArray()
  virtual void unmapArray(ANARIArray) = 0;

  // Implement anariGetProperty()
  virtual int getProperty(ANARIObject object,
      const char *name,
      ANARIDataType type,
      void *mem,
      uint64_t size,
      ANARIWaitMask mask) = 0;

  // Object Query Interface ///////////////////////////////////////////////////

  // Implement anariGetObjectSubtypes()
  virtual const char **getObjectSubtypes(ANARIDataType objectType) = 0;

  // Implement anariGetObjectInfo()
  virtual const void *getObjectInfo(ANARIDataType objectType,
      const char *objectSubtype,
      const char *infoName,
      ANARIDataType infoType) = 0;

  // Implement anariGetParameterInfo()
  virtual const void *getParameterInfo(ANARIDataType objectType,
      const char *objectSubtype,
      const char *parameterName,
      ANARIDataType parameterType,
      const char *infoName,
      ANARIDataType infoType) = 0;

  // FrameBuffer Manipulation /////////////////////////////////////////////////

  // Implement anariFrameBufferMap
  virtual const void *frameBufferMap(ANARIFrame fb,
      const char *channel,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType) = 0;

  // Implement anariFrameBufferUnmap
  virtual void frameBufferUnmap(ANARIFrame fb, const char *channel) = 0;

  // Frame Rendering //////////////////////////////////////////////////////////

  // Implement anariRenderFrame()
  virtual void renderFrame(ANARIFrame) = 0;

  // Implement anariFrameReady()
  virtual int frameReady(ANARIFrame, ANARIWaitMask) = 0;

  // Implement anariDiscardFrame()
  virtual void discardFrame(ANARIFrame) = 0;

  /////////////////////////////////////////////////////////////////////////////
  // Extension interface
  /////////////////////////////////////////////////////////////////////////////

  // Optionally allow creation of extension objects that do not fit any core
  // handle types.
  virtual ANARIObject newObject(const char *objectType, const char *type);

  // Optionally allow dynamic lookup of special extension functions
  virtual void (*getProcAddress(const char *name))(void);

  /////////////////////////////////////////////////////////////////////////////
  // Helper/other functions and data members
  /////////////////////////////////////////////////////////////////////////////

  DeviceImpl(ANARILibrary);
  DeviceImpl() = default;
  virtual ~DeviceImpl() = default;

  // Utility to get 'this' as an ANARIDevice handle
  ANARIDevice this_device() const;

  ANARIStatusCallback m_defaultStatusCB{nullptr};
  const void *m_defaultStatusCBUserPtr{nullptr};

 protected:
  ANARIStatusCallback defaultStatusCallback() const;
  const void *defaultStatusCallbackUserPtr() const;

  bool handleIsDevice(ANARIObject obj) const;
};

ANARI_TYPEFOR_SPECIALIZATION(DeviceImpl *, ANARI_DEVICE);

} // namespace anari
