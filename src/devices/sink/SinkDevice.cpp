// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SinkDevice.h"

#include "anari/anari_cpp.hpp"

#include <cstring>

namespace sink_device {

///////////////////////////////////////////////////////////////////////////////
// SinkDevice definitions /////////////////////////////////////////////////////
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
    uint64_t numItems)
{
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
    uint64_t numItems2)
{
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
    uint64_t numItems3)
{
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

ANARIInstance SinkDevice::newInstance(const char *type)
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

const char **SinkDevice::getObjectSubtypes(ANARIDataType objectType)
{
  return sink_device::query_object_types(objectType);
}

const void *SinkDevice::getObjectInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *infoName,
    ANARIDataType infoType)
{
  return sink_device::query_object_info(
      objectType, objectSubtype, infoName, infoType);
}

const void *SinkDevice::getParameterInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *parameterName,
    ANARIDataType parameterType,
    const char *infoName,
    ANARIDataType infoType)
{
  return sink_device::query_param_info(objectType,
      objectSubtype,
      parameterName,
      parameterType,
      infoName,
      infoType);
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

void SinkDevice::unsetAllParameters(ANARIObject) {}

void *SinkDevice::mapParameterArray1D(ANARIObject object,
    const char *name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t *elementStride)
{
  if (auto obj = getObject(object)) {
    if (elementStride)
      *elementStride = sizeOf(dataType);
    return obj->mapArray(name, sizeOf(dataType) * numElements1);
  } else {
    return nullptr;
  }
}

void *SinkDevice::mapParameterArray2D(ANARIObject object,
    const char *name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t numElements2,
    uint64_t *elementStride)
{
  if (auto obj = getObject(object)) {
    if (elementStride)
      *elementStride = sizeOf(dataType);
    return obj->mapArray(name, sizeOf(dataType) * numElements1 * numElements2);
  } else {
    return nullptr;
  }
}

void *SinkDevice::mapParameterArray3D(ANARIObject object,
    const char *name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t numElements2,
    uint64_t numElements3,
    uint64_t *elementStride)
{
  if (auto obj = getObject(object)) {
    if (elementStride)
      *elementStride = sizeOf(dataType);
    return obj->mapArray(
        name, sizeOf(dataType) * numElements1 * numElements2 * numElements3);
  } else {
    return nullptr;
  }
}

void SinkDevice::unmapParameterArray(ANARIObject object, const char *name) {}

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
      *pixelType = ANARI_FLOAT32_VEC4;
      return obj->memory;
    }
  }
  *width = 0;
  *height = 0;
  *pixelType = ANARI_UNKNOWN;
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

// Other SinkDevice definitions ///////////////////////////////////////////////

SinkDevice::SinkDevice(ANARILibrary library) : DeviceImpl(library)
{
  nextHandle<ANARIObject>(); // insert a handle at 0
}

const char **query_extensions();

} // namespace sink_device
