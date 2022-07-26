// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/backend/utilities/IntrusivePtr.h"
// anari
#include "anari/backend/DeviceImpl.h"

#include <memory>
#include <vector>

#ifdef _WIN32
#ifdef SINK_DEVICE_STATIC_DEFINE
#define SINK_DEVICE_INTERFACE
#else
#ifdef anari_library_sink_EXPORTS
#define SINK_DEVICE_INTERFACE __declspec(dllexport)
#else
#define SINK_DEVICE_INTERFACE __declspec(dllimport)
#endif
#endif
#elif defined __GNUC__
#define SINK_DEVICE_INTERFACE __attribute__((__visibility__("default")))
#else
#define SINK_DEVICE_INTERFACE
#endif

namespace anari {
namespace sink_device {

struct SINK_DEVICE_INTERFACE SinkDevice : public DeviceImpl, public RefCounted
{
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

  SinkDevice(ANARILibrary);
  ~SinkDevice() = default;

 private:
  struct Object
  {
    int64_t refcount = 1;
    ANARIMemoryDeleter deleter = nullptr;
    const void *userdata = nullptr;
    const void *memory = nullptr;
    ANARIDataType type;

    void *map()
    {
      return const_cast<void *>(memory);
    }
    void retain()
    {
      refcount += 1;
    }
    void release()
    {
      refcount -= 1;
      if (refcount == 0 && deleter) {
        deleter(userdata, memory);
        deleter = nullptr;
      }
    }
    Object(ANARIDataType type) : type(type) {}
    ~Object()
    {
      if (deleter) {
        deleter(userdata, memory);
      }
    }
  };

  std::vector<std::unique_ptr<Object>> objects;

  template <typename T>
  T nextHandle()
  {
    uintptr_t next = objects.size();
    objects.emplace_back(new Object(ANARITypeFor<T>::value));
    return reinterpret_cast<T>(next);
  }

  template <typename T>
  Object *getObject(T handle)
  {
    uintptr_t index = reinterpret_cast<uintptr_t>(handle);
    if (index < objects.size()) {
      return objects[index].get();
    } else {
      return nullptr;
    }
  }
};

} // namespace sink_device
} // namespace anari
