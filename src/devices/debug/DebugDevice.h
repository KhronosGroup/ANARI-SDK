// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/utility/IntrusivePtr.h"
// anari
#include "anari/backend/DeviceImpl.h"

#include "anari/anari_cpp.hpp"

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

#include "anari_library_debug_queries.h"

namespace anari {
namespace debug_device {

struct DEBUG_DEVICE_INTERFACE DebugDevice : public DeviceImpl, public helium::RefCounted
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

  ANARIInstance newInstance(const char *type) override;

  // Top-level Worlds /////////////////////////////////////////////////////////

  ANARIWorld newWorld() override;

  // Query functions //////////////////////////////////////////////////////////

  const char ** getObjectSubtypes(ANARIDataType objectType) override;
  const void* getObjectInfo(ANARIDataType objectType,
      const char* objectSubtype,
      const char* infoName,
      ANARIDataType infoType) override;
  const void* getParameterInfo(ANARIDataType objectType,
      const char* objectSubtype,
      const char* parameterName,
      ANARIDataType parameterType,
      const char* infoName,
      ANARIDataType infoType) override;

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
  void unsetAllParameters(ANARIObject object) override;

  void* mapParameterArray1D(ANARIObject object,
      const char* name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t* elementStride) override;
  void* mapParameterArray2D(ANARIObject object,
      const char* name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t numElements2,
      uint64_t* elementStride) override;
  void* mapParameterArray3D(ANARIObject object,
      const char* name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t numElements2,
      uint64_t numElements3,
      uint64_t* elementStride) override;
  void unmapParameterArray(ANARIObject object,
      const char* name) override;

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

  ANARIDevice getWrapped() const { return wrapped; }

 private:
  std::string wrappedLibraryFromEnv;
  ANARIDevice wrapped{nullptr};
  ANARIDevice staged{nullptr};

  DebugObject<ANARI_DEVICE> deviceInfo;

  std::unordered_map<ANARIObject, ANARIObject> objectMap;
  std::vector<char> last_status_message;

  std::unique_ptr<DebugInterface> debug;
  ObjectFactory *debugObjectFactory{nullptr};

  std::unique_ptr<SerializerInterface> serializer;
  SerializerInterface *(*createSerializer)(DebugDevice *) = nullptr;

 public:
  std::string traceDir;
};

} // namespace debug_device
} // namespace anari
