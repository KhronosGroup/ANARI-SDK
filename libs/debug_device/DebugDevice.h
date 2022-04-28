// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/detail/IntrusivePtr.h"
// anari
#include "anari/detail/Device.h"

#include "anari/type_utility.h"

// std
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <cstring>

#include "debug_device_exports.h"

#include "DebugInterface.h"
#include "DebugObject.h"

namespace anari {
namespace debug_device {

struct DEBUG_DEVICE_INTERFACE DebugDevice : public Device,
                                              public RefCounted
{

  void reportStatus(
      ANARIObject source,
      ANARIDataType sourceType,
      ANARIStatusSeverity severity,
      ANARIStatusCode code,
      const char *format, ...);

  /////////////////////////////////////////////////////////////////////////////
  // Main interface to accepting API calls
  /////////////////////////////////////////////////////////////////////////////

  // Device API ///////////////////////////////////////////////////////////////

  int deviceImplements(const char *extension) override;

  void deviceSetParameter(
      const char *id, ANARIDataType type, const void *mem) override;

  void deviceUnsetParameter(const char *id) override;

  void deviceCommit() override;

  void deviceRetain() override;

  void deviceRelease() override;

  // Data Arrays //////////////////////////////////////////////////////////////

  ANARIArray1D newArray1D(void *appMemory,
      ANARIMemoryDeleter deleter,
      void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t byteStride1) override;

  ANARIArray2D newArray2D(void *appMemory,
      ANARIMemoryDeleter deleter,
      void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t byteStride1,
      uint64_t byteStride2) override;

  ANARIArray3D newArray3D(void *appMemory,
      ANARIMemoryDeleter deleter,
      void *userdata,
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

  void commit(ANARIObject object) override;

  void release(ANARIObject _obj) override;
  void retain(ANARIObject _obj) override;

  // FrameBuffer Manipulation /////////////////////////////////////////////////

  ANARIFrame newFrame() override;

  const void *frameBufferMap(ANARIFrame fb, const char *channel) override;

  void frameBufferUnmap(ANARIFrame fb, const char *channel) override;

  // Frame Rendering //////////////////////////////////////////////////////////

  ANARIRenderer newRenderer(const char *type) override;

  void renderFrame(ANARIFrame) override;
  int frameReady(ANARIFrame, ANARIWaitMask) override;
  void discardFrame(ANARIFrame) override;

  /////////////////////////////////////////////////////////////////////////////
  // Helper/other functions and data members
  /////////////////////////////////////////////////////////////////////////////

  DebugDevice();
  ~DebugDevice();

  ANARIObject newObjectHandle(ANARIObject, ANARIDataType);
  ANARIObject newObjectHandle(ANARIObject, ANARIDataType, const char*);
  ANARIObject wrapObjectHandle(ANARIObject, ANARIDataType = ANARI_OBJECT);
  ANARIObject unwrapObjectHandle(ANARIObject, ANARIDataType = ANARI_OBJECT);
  DebugObjectBase* getObjectInfo(ANARIObject);

  template<typename T>
  T newHandle(T o) { return static_cast<T>(newObjectHandle(o, ANARITypeFor<T>::value)); }
  template<typename T>
  T newHandle(T o, const char *name) { return static_cast<T>(newObjectHandle(o, ANARITypeFor<T>::value, name)); }
  template<typename T>
  T wrapHandle(T o) { return static_cast<T>(wrapObjectHandle(o, ANARITypeFor<T>::value)); }
  template<typename T>
  T unwrapHandle(T o) { return static_cast<T>(unwrapObjectHandle(o, ANARITypeFor<T>::value)); }

  template<typename T>
  T* getDynamicObjectInfo(ANARIObject o) { return dynamic_cast<T*>(getObjectInfo(o)); }

  std::vector<std::unique_ptr<DebugObjectBase>> objects;
 private:
  DebugObject<ANARI_DEVICE> deviceInfo;

  std::unordered_map<ANARIObject, ANARIObject> objectMap;
  std::vector<char> last_status_message;

  std::unique_ptr<DebugInterface> debug;
  ObjectFactory *debugObjectFactory;

  ANARIDevice wrapped;
  ANARIDevice staged;
};

} // namespace example_device
} // namespace anari
