// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include "anari/anari.h"
#include "anari/anari_cpp/Traits.h"

#include "anari/backend/LibraryImpl.h"

// std
#include <map>
#include <string>
#include <vector>

namespace anari {

template <typename T, typename... Args>
using FactoryFcn = T *(*)(Args...);

template <typename T, typename... Args>
using FactoryMap = std::map<std::string, FactoryFcn<T, Args...>>;

template <typename T, typename... Args>
using FactoryVector = std::vector<FactoryFcn<T, Args...>>;

// should this use Args&&... and forwarding?
template <typename B, typename T, typename... Args>
inline B *allocate_object(Args... args)
{
  return new T(args...);
}

struct ANARI_INTERFACE DeviceImpl
{
  /////////////////////////////////////////////////////////////////////////////
  // Main virtual interface to accepting API calls
  /////////////////////////////////////////////////////////////////////////////

  // Data Arrays //////////////////////////////////////////////////////////////

  virtual ANARIArray1D newArray1D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t byteStride1) = 0;

  virtual ANARIArray2D newArray2D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t byteStride1,
      uint64_t byteStride2) = 0;

  virtual ANARIArray3D newArray3D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t numItems3,
      uint64_t byteStride1,
      uint64_t byteStride2,
      uint64_t byteStride3) = 0;

  virtual void *mapArray(ANARIArray) = 0;
  virtual void unmapArray(ANARIArray) = 0;

  // Renderable Objects ///////////////////////////////////////////////////////

  virtual ANARILight newLight(const char *type) = 0;

  virtual ANARICamera newCamera(const char *type) = 0;

  virtual ANARIGeometry newGeometry(const char *type) = 0;
  virtual ANARISpatialField newSpatialField(const char *type) = 0;

  virtual ANARISurface newSurface() = 0;
  virtual ANARIVolume newVolume(const char *type) = 0;

  // Surface Meta-Data ////////////////////////////////////////////////////////

  virtual ANARIMaterial newMaterial(const char *material_type) = 0;

  virtual ANARISampler newSampler(const char *type) = 0;

  // Instancing ///////////////////////////////////////////////////////////////

  virtual ANARIGroup newGroup() = 0;

  virtual ANARIInstance newInstance() = 0;

  // Top-level Worlds /////////////////////////////////////////////////////////

  virtual ANARIWorld newWorld() = 0;

  // Object + Parameter Lifetime Management ///////////////////////////////////

  virtual void setParameter(ANARIObject object,
      const char *name,
      ANARIDataType type,
      const void *mem) = 0;

  virtual void unsetParameter(ANARIObject object, const char *name) = 0;

  virtual void commitParameters(ANARIObject object) = 0;

  virtual void release(ANARIObject _obj) = 0;
  virtual void retain(ANARIObject _obj) = 0;

  // Object Query Interface ///////////////////////////////////////////////////

  virtual int getProperty(ANARIObject object,
      const char *name,
      ANARIDataType type,
      void *mem,
      uint64_t size,
      ANARIWaitMask mask) = 0;

  // FrameBuffer Manipulation /////////////////////////////////////////////////

  virtual ANARIFrame newFrame() = 0;

  virtual const void *frameBufferMap(ANARIFrame fb,
      const char *channel,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType) = 0;

  virtual void frameBufferUnmap(ANARIFrame fb, const char *channel) = 0;

  // Frame Rendering //////////////////////////////////////////////////////////

  virtual ANARIRenderer newRenderer(const char *type) = 0;

  virtual void renderFrame(ANARIFrame) = 0;
  virtual int frameReady(ANARIFrame, ANARIWaitMask) = 0;
  virtual void discardFrame(ANARIFrame) = 0;

  /////////////////////////////////////////////////////////////////////////////
  // Extension interface
  /////////////////////////////////////////////////////////////////////////////

  virtual ANARIObject newObject(const char *objectType, const char *type);

  virtual void (*getProcAddress(const char *name))(void);

  /////////////////////////////////////////////////////////////////////////////
  // Helper/other functions and data members
  /////////////////////////////////////////////////////////////////////////////

  DeviceImpl(ANARILibrary);
  DeviceImpl() = default;
  virtual ~DeviceImpl() = default;

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
