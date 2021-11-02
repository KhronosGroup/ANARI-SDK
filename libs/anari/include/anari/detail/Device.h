// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include "anari/anari.h"
#include "anari/anari_cpp/Traits.h"
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

struct Device
{
  /////////////////////////////////////////////////////////////////////////////
  // Main virtual interface to accepting API calls
  /////////////////////////////////////////////////////////////////////////////

  // Device API ///////////////////////////////////////////////////////////////

  ANARI_INTERFACE virtual int deviceImplements(const char *extension) = 0;

  ANARI_INTERFACE virtual void deviceSetParameter(
      const char *id, ANARIDataType type, const void *mem) = 0;

  ANARI_INTERFACE virtual void deviceUnsetParameter(const char *id) = 0;

  ANARI_INTERFACE virtual void deviceCommit() = 0;

  ANARI_INTERFACE virtual void deviceRetain() = 0;

  ANARI_INTERFACE virtual void deviceRelease() = 0;

  // Data Arrays //////////////////////////////////////////////////////////////

  ANARI_INTERFACE virtual ANARIArray1D newArray1D(void *appMemory,
      ANARIMemoryDeleter deleter,
      void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t byteStride1) = 0;

  ANARI_INTERFACE virtual ANARIArray2D newArray2D(void *appMemory,
      ANARIMemoryDeleter deleter,
      void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t byteStride1,
      uint64_t byteStride2) = 0;

  ANARI_INTERFACE virtual ANARIArray3D newArray3D(void *appMemory,
      ANARIMemoryDeleter deleter,
      void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t numItems3,
      uint64_t byteStride1,
      uint64_t byteStride2,
      uint64_t byteStride3) = 0;

  ANARI_INTERFACE virtual void *mapArray(ANARIArray) = 0;
  ANARI_INTERFACE virtual void unmapArray(ANARIArray) = 0;

  ANARI_INTERFACE virtual void arrayRangeUpdated1D(
      ANARIArray1D, uint64_t startIndex1, uint64_t elementCount1) = 0;

  ANARI_INTERFACE virtual void arrayRangeUpdated2D(ANARIArray2D,
      uint64_t startIndex1,
      uint64_t startIndex2,
      uint64_t elementCount1,
      uint64_t elementCount2) = 0;

  ANARI_INTERFACE virtual void arrayRangeUpdated3D(ANARIArray3D,
      uint64_t startIndex1,
      uint64_t startIndex2,
      uint64_t startIndex3,
      uint64_t elementCount1,
      uint64_t elementCount2,
      uint64_t elementCount3) = 0;

  // Renderable Objects ///////////////////////////////////////////////////////

  ANARI_INTERFACE virtual ANARILight newLight(const char *type) = 0;

  ANARI_INTERFACE virtual ANARICamera newCamera(const char *type) = 0;

  ANARI_INTERFACE virtual ANARIGeometry newGeometry(const char *type) = 0;
  ANARI_INTERFACE virtual ANARISpatialField newSpatialField(
      const char *type) = 0;

  ANARI_INTERFACE virtual ANARISurface newSurface() = 0;
  ANARI_INTERFACE virtual ANARIVolume newVolume(const char *type) = 0;

  // Surface Meta-Data ////////////////////////////////////////////////////////

  ANARI_INTERFACE virtual ANARIMaterial newMaterial(
      const char *material_type) = 0;

  ANARI_INTERFACE virtual ANARISampler newSampler(const char *type) = 0;

  // Instancing ///////////////////////////////////////////////////////////////

  ANARI_INTERFACE virtual ANARIGroup newGroup() = 0;

  ANARI_INTERFACE virtual ANARIInstance newInstance() = 0;

  // Top-level Worlds /////////////////////////////////////////////////////////

  ANARI_INTERFACE virtual ANARIWorld newWorld() = 0;

  // Object + Parameter Lifetime Management ///////////////////////////////////

  ANARI_INTERFACE virtual void setParameter(ANARIObject object,
      const char *name,
      ANARIDataType type,
      const void *mem) = 0;

  ANARI_INTERFACE virtual void unsetParameter(
      ANARIObject object, const char *name) = 0;

  ANARI_INTERFACE virtual void commit(ANARIObject object) = 0;

  ANARI_INTERFACE virtual void release(ANARIObject _obj) = 0;
  ANARI_INTERFACE virtual void retain(ANARIObject _obj) = 0;

  // Object Query Interface ///////////////////////////////////////////////////

  ANARI_INTERFACE virtual int getProperty(ANARIObject object,
      const char *name,
      ANARIDataType type,
      void *mem,
      uint64_t size,
      ANARIWaitMask mask) = 0;

  // FrameBuffer Manipulation /////////////////////////////////////////////////

  ANARI_INTERFACE virtual ANARIFrame frameCreate() = 0;

  ANARI_INTERFACE virtual const void *frameBufferMap(
      ANARIFrame fb, const char *channel) = 0;

  ANARI_INTERFACE virtual void frameBufferUnmap(
      ANARIFrame fb, const char *channel) = 0;

  // Frame Rendering //////////////////////////////////////////////////////////

  ANARI_INTERFACE virtual ANARIRenderer newRenderer(const char *type) = 0;

  ANARI_INTERFACE virtual void renderFrame(ANARIFrame) = 0;
  ANARI_INTERFACE virtual int frameReady(ANARIFrame, ANARIWaitMask) = 0;
  ANARI_INTERFACE virtual void discardFrame(ANARIFrame) = 0;

  /////////////////////////////////////////////////////////////////////////////
  // Extension interface
  /////////////////////////////////////////////////////////////////////////////

  ANARI_INTERFACE virtual ANARIObject newObject(
      const char *objectType, const char *type);

  ANARI_INTERFACE virtual void (*getProcAddress(const char *name))(void);

  /////////////////////////////////////////////////////////////////////////////
  // Helper/other functions and data members
  /////////////////////////////////////////////////////////////////////////////

  ANARI_INTERFACE Device() = default;
  ANARI_INTERFACE virtual ~Device() = default;

  ANARI_INTERFACE static Device *createDevice(const char *type,
      ANARIStatusCallback defaultStatusCB = nullptr,
      void *defaultStatusCBUserPtr = nullptr);

  template <typename T>
  static void registerType(const char *type);

  template <typename T>
  static void registerLayer();

 protected:
  ANARIDevice this_device() const;

  ANARI_INTERFACE ANARIStatusCallback defaultStatusCallback() const;
  ANARI_INTERFACE void *defaultStatusCallbackUserPtr() const;

 private:
  ANARI_INTERFACE static void registerType(
      const char *type, FactoryFcn<Device> f);

  ANARI_INTERFACE static void registerLayer(FactoryFcn<Device, Device *> f);

  ANARIStatusCallback m_defaultStatusCB{nullptr};
  void *m_defaultStatusCBUserPtr{nullptr};
};

ANARI_TYPEFOR_SPECIALIZATION(Device *, ANARI_DEVICE);

// Inlined defintions /////////////////////////////////////////////////////////

template <typename T>
inline void Device::registerType(const char *type)
{
  Device *(*fcn)() = &allocate_object<Device, T>;
  Device::registerType(type, fcn);
}

template <typename T>
inline void Device::registerLayer()
{
  FactoryFcn<Device, Device *> fcn = &allocate_object<Device, T, Device *>;
  Device::registerLayer(fcn);
}

} // namespace anari
