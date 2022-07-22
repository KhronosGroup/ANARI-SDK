// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SinkDevice.h"

#include "anari/anari.h"
#include "anari/backend/LibraryImpl.h"
#include "anari/type_utility.h"

#include <cstring>

namespace anari {
namespace sink_device {

///////////////////////////////////////////////////////////////////////////////
// SinkDevice definitions //////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// Data Arrays ////////////////////////////////////////////////////////////////

void managed_deleter(const void *, const void *memory)
{
  delete[] static_cast<char *>(const_cast<void *>(memory));
}

ANARIArray1D SinkDevice::newArray1D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems,
    uint64_t byteStride)
{
  (void)byteStride;
  ANARIArray1D handle = nextHandle<ANARIArray1D>();
  if (auto obj = getObject(handle)) {
    if (appMemory == nullptr) {
      obj->userdata = nullptr;
      obj->memory = new char[sizeOf(type) * numItems];
      obj->deleter = managed_deleter;
    } else {
      obj->userdata = userData;
      obj->memory = appMemory;
      obj->deleter = deleter;
    }
  }
  return handle;
}

ANARIArray2D SinkDevice::newArray2D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t byteStride1,
    uint64_t byteStride2)
{
  (void)byteStride1;
  (void)byteStride2;
  ANARIArray2D handle = nextHandle<ANARIArray2D>();
  if (auto obj = getObject(handle)) {
    if (appMemory == nullptr) {
      obj->userdata = nullptr;
      obj->memory = new char[sizeOf(type) * numItems1 * numItems2];
      obj->deleter = managed_deleter;
    } else {
      obj->userdata = userData;
      obj->memory = appMemory;
      obj->deleter = deleter;
    }
  }
  return handle;
}

ANARIArray3D SinkDevice::newArray3D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3,
    uint64_t byteStride1,
    uint64_t byteStride2,
    uint64_t byteStride3)
{
  (void)byteStride1;
  (void)byteStride2;
  (void)byteStride3;
  ANARIArray3D handle = nextHandle<ANARIArray3D>();
  if (auto obj = getObject(handle)) {
    if (appMemory == nullptr) {
      obj->userdata = nullptr;
      obj->memory = new char[sizeOf(type) * numItems1 * numItems2 * numItems3];
      obj->deleter = managed_deleter;
    } else {
      obj->userdata = userData;
      obj->memory = appMemory;
      obj->deleter = deleter;
    }
  }
  return handle;
}

void *SinkDevice::mapArray(ANARIArray a)
{
  if (auto obj = getObject(a)) {
    return const_cast<void *>(obj->memory);
  } else {
    return nullptr;
  }
}

void SinkDevice::unmapArray(ANARIArray) {}

// Renderable Objects /////////////////////////////////////////////////////////

ANARILight SinkDevice::newLight(const char *)
{
  return nextHandle<ANARILight>();
}

ANARICamera SinkDevice::newCamera(const char *)
{
  return nextHandle<ANARICamera>();
}

ANARIGeometry SinkDevice::newGeometry(const char *)
{
  return nextHandle<ANARIGeometry>();
}

ANARISpatialField SinkDevice::newSpatialField(const char *)
{
  return nextHandle<ANARISpatialField>();
}

ANARISurface SinkDevice::newSurface()
{
  return nextHandle<ANARISurface>();
}

ANARIVolume SinkDevice::newVolume(const char *)
{
  return nextHandle<ANARIVolume>();
}

// Model Meta-Data ////////////////////////////////////////////////////////////

ANARIMaterial SinkDevice::newMaterial(const char *)
{
  return nextHandle<ANARIMaterial>();
}

ANARISampler SinkDevice::newSampler(const char *)
{
  return nextHandle<ANARISampler>();
}

// Instancing /////////////////////////////////////////////////////////////////

ANARIGroup SinkDevice::newGroup()
{
  return nextHandle<ANARIGroup>();
}

ANARIInstance SinkDevice::newInstance()
{
  return nextHandle<ANARIInstance>();
}

// Top-level Worlds ///////////////////////////////////////////////////////////

ANARIWorld SinkDevice::newWorld()
{
  return nextHandle<ANARIWorld>();
}

int SinkDevice::getProperty(
    ANARIObject, const char *, ANARIDataType, void *, uint64_t, ANARIWaitMask)
{
  return 0;
}

// Object + Parameter Lifetime Management /////////////////////////////////////

struct FrameData
{
  uint32_t width = 1;
  uint32_t height = 1;
};

void frame_deleter(const void *userdata, const void *memory)
{
  delete[] static_cast<char *>(const_cast<void *>(memory));
  delete static_cast<FrameData *>(const_cast<void *>(userdata));
}

void SinkDevice::setParameter(
    ANARIObject object, const char *name, ANARIDataType type, const void *mem)
{
  if (auto obj = getObject(object)) {
    if (obj->type == ANARI_FRAME) {
      FrameData *data =
          static_cast<FrameData *>(const_cast<void *>(obj->userdata));
      if (type == ANARI_UINT32_VEC2 && std::strncmp("size", name, 4) == 0) {
        const uint32_t *size = static_cast<const uint32_t *>(mem);
        data->width = size[0];
        data->height = size[1];
        delete[] static_cast<char *>(const_cast<void *>(obj->memory));
        obj->memory = nullptr;
      }
    }
  }
}

void SinkDevice::unsetParameter(ANARIObject, const char *) {}

void SinkDevice::commitParameters(ANARIObject) {}

void SinkDevice::release(ANARIObject object)
{
  if (auto obj = getObject(object)) {
    obj->release();
  }
}

void SinkDevice::retain(ANARIObject object)
{
  if (auto obj = getObject(object)) {
    obj->retain();
  }
}

// Frame Manipulation /////////////////////////////////////////////////////////

ANARIFrame SinkDevice::newFrame()
{
  ANARIFrame frame = nextHandle<ANARIFrame>();
  if (auto obj = getObject(frame)) {
    obj->userdata = new FrameData();
    obj->deleter = frame_deleter;
  }
  return frame;
}

const void *SinkDevice::frameBufferMap(ANARIFrame fb,
    const char *,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *pixelType)
{
  if (auto obj = getObject(fb)) {
    if (obj->type == ANARI_FRAME) {
      const FrameData *data = static_cast<const FrameData *>(obj->userdata);
      if (obj->memory == nullptr) {
        obj->memory = new char[data->width * data->height * 4 * sizeof(float)];
      }
      *width = data->width;
      *height = data->height;
      *pixelType = ANARI_FLOAT32;
      return obj->memory;
    }
  }
  return nullptr;
}

void SinkDevice::frameBufferUnmap(ANARIFrame, const char *) {}

// Frame Rendering ////////////////////////////////////////////////////////////

ANARIRenderer SinkDevice::newRenderer(const char *)
{
  return nextHandle<ANARIRenderer>();
}

void SinkDevice::renderFrame(ANARIFrame) {}

int SinkDevice::frameReady(ANARIFrame, ANARIWaitMask)
{
  return 1;
}

void SinkDevice::discardFrame(ANARIFrame) {}

// Other SinkDevice definitions ////////////////////////////////////////////

SinkDevice::SinkDevice(ANARILibrary library) : DeviceImpl(library)
{
  nextHandle<ANARIObject>(); // insert a handle at 0
}

const char **query_object_types(ANARIDataType type);
const void *query_object_info(ANARIDataType type,
    const char *subtype,
    const char *infoName,
    ANARIDataType infoType);
const void *query_param_info(ANARIDataType type,
    const char *subtype,
    const char *paramName,
    ANARIDataType paramType,
    const char *infoName,
    ANARIDataType infoType);

} // namespace sink_device
} // namespace anari

static char deviceName[] = "sink";

extern "C" SINK_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_NEW_DEVICE(
    sink, library, subtype)
{
  if (subtype == std::string("default") || subtype == std::string("sink"))
    return (ANARIDevice) new anari::sink_device::SinkDevice(library);
  return nullptr;
}

extern "C" SINK_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_INIT(sink) {}

extern "C" SINK_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_DEVICE_SUBTYPES(
    sink, library)
{
  (void)library;
  static const char *devices[] = {deviceName, nullptr};
  return devices;
}

extern "C" SINK_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_OBJECT_SUBTYPES(
    sink, library, deviceSubtype, objectType)
{
  (void)library;
  (void)deviceSubtype;
  return anari::sink_device::query_object_types(objectType);
}

extern "C" SINK_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_OBJECT_PROPERTY(sink,
    library,
    deviceSubtype,
    objectSubtype,
    objectType,
    propertyName,
    propertyType)
{
  (void)library;
  (void)deviceSubtype;
  return anari::sink_device::query_object_info(
      objectType, objectSubtype, propertyName, propertyType);
}

extern "C" SINK_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_PARAMETER_PROPERTY(
    sink,
    library,
    deviceSubtype,
    objectSubtype,
    objectType,
    parameterName,
    parameterType,
    propertyName,
    propertyType)
{
  (void)library;
  (void)deviceSubtype;
  return anari::sink_device::query_param_info(objectType,
      objectSubtype,
      parameterName,
      parameterType,
      propertyName,
      propertyType);
}
