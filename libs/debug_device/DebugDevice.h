// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/backend/utilities/IntrusivePtr.h"
// anari
#include "anari/backend/DeviceImpl.h"

#include "anari/type_utility.h"

// std
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "DebugInterface.h"
#include "DebugSerializerInterface.h"

#include "anari/ext/debug/DebugObject.h"

#include <cstdarg>

#include "ExtendedQueries.h"

namespace anari {
namespace debug_device {

struct DEBUG_DEVICE_INTERFACE DebugDevice : public DeviceImpl, public RefCounted
{
  void reportStatus(ANARIObject source,
      ANARIDataType sourceType,
      ANARIStatusSeverity severity,
      ANARIStatusCode code,
      const char *format,
      ...);

  void vreportStatus(ANARIObject source,
      ANARIDataType sourceType,
      ANARIStatusSeverity severity,
      ANARIStatusCode code,
      const char *format,
      va_list);

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

  void deviceSetParameter(const char *id, ANARIDataType type, const void *mem);
  void deviceUnsetParameter(const char *id);
  void deviceCommit();

  DebugDevice(ANARILibrary);
  ~DebugDevice();

  ANARIObject newObjectHandle(ANARIObject, ANARIDataType);
  ANARIObject newObjectHandle(ANARIObject, ANARIDataType, const char *);
  ANARIObject wrapObjectHandle(ANARIObject, ANARIDataType = ANARI_OBJECT);
  ANARIObject unwrapObjectHandle(ANARIObject, ANARIDataType = ANARI_OBJECT);
  DebugObjectBase *getObjectInfo(ANARIObject);

  template <typename T>
  T newHandle(T o)
  {
    return static_cast<T>(newObjectHandle(o, ANARITypeFor<T>::value));
  }
  template <typename T>
  T newHandle(T o, const char *name)
  {
    return static_cast<T>(newObjectHandle(o, ANARITypeFor<T>::value, name));
  }
  template <typename T>
  T wrapHandle(T o)
  {
    return static_cast<T>(wrapObjectHandle(o, ANARITypeFor<T>::value));
  }
  template <typename T>
  T unwrapHandle(T o)
  {
    return static_cast<T>(unwrapObjectHandle(o, ANARITypeFor<T>::value));
  }

  template <typename T>
  T *getDynamicObjectInfo(ANARIObject o)
  {
    return dynamic_cast<T *>(getObjectInfo(o));
  }

  int used_features[anari::debug_queries::extension_count] = {};
  int unknown_feature_uses = 0;
  void reportParameterUse(ANARIDataType objtype,
      const char *objSubtype,
      const char *paramname,
      ANARIDataType paramtype);
  void reportObjectUse(ANARIDataType objtype, const char *objSubtype);

  std::vector<std::unique_ptr<DebugObjectBase>> objects;

 private:
  ANARIDevice wrapped;
  ANARIDevice staged;

  DebugObject<ANARI_DEVICE> deviceInfo;

  std::unordered_map<ANARIObject, ANARIObject> objectMap;
  std::vector<char> last_status_message;

  std::unique_ptr<DebugInterface> debug;
  ObjectFactory *debugObjectFactory;

  std::unique_ptr<SerializerInterface> serializer;
  SerializerInterface *(*createSerializer)(DebugDevice *) = nullptr;

 public:
  std::string traceDir;
};

} // namespace debug_device
} // namespace anari
