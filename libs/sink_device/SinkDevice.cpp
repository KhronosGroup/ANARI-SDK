// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SinkDevice.h"

#include "anari/detail/Library.h"
#include "anari/anari.h"
#include "anari/type_utility.h"

#include <cstring>

namespace anari {
namespace sink_device {

///////////////////////////////////////////////////////////////////////////////
// SinkDevice definitions //////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int SinkDevice::deviceImplements(const char *_extension)
{
  return 0;
}

void SinkDevice::deviceSetParameter(
    const char *_id, ANARIDataType type, const void *mem)
{

}

void SinkDevice::deviceUnsetParameter(const char *id)
{

}

void SinkDevice::deviceCommit()
{

}

void SinkDevice::deviceRetain()
{

}

void SinkDevice::deviceRelease()
{

}

// Data Arrays ////////////////////////////////////////////////////////////////

void managed_deleter(void *userdata, void *memory) {
  delete[] static_cast<char*>(memory);
}

ANARIArray1D SinkDevice::newArray1D(void *appMemory,
    ANARIMemoryDeleter deleter,
    void *userData,
    ANARIDataType type,
    uint64_t numItems,
    uint64_t byteStride)
{
  ANARIArray1D handle = nextHandle<ANARIArray1D>();
  if(auto obj = getObject(handle)) {
    if(appMemory == nullptr) {
      obj->userdata = nullptr;
      obj->memory = new char[sizeOf(type)*numItems];
      obj->deleter = managed_deleter;
    } else {
      obj->userdata = userData;
      obj->memory = appMemory;
      obj->deleter = deleter;
    }
  }
  return handle;
}

ANARIArray2D SinkDevice::newArray2D(void *appMemory,
    ANARIMemoryDeleter deleter,
    void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t byteStride1,
    uint64_t byteStride2)
{
  ANARIArray2D handle = nextHandle<ANARIArray2D>();
  if(auto obj = getObject(handle)) {
    if(appMemory == nullptr) {
      obj->userdata = nullptr;
      obj->memory = new char[sizeOf(type)*numItems1*numItems2];
      obj->deleter = managed_deleter;
    } else {
      obj->userdata = userData;
      obj->memory = appMemory;
      obj->deleter = deleter;
    }
  }
  return handle;
}

ANARIArray3D SinkDevice::newArray3D(void *appMemory,
    ANARIMemoryDeleter deleter,
    void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3,
    uint64_t byteStride1,
    uint64_t byteStride2,
    uint64_t byteStride3)
{
  ANARIArray3D handle = nextHandle<ANARIArray3D>();
  if(auto obj = getObject(handle)) {
    if(appMemory == nullptr) {
      obj->userdata = nullptr;
      obj->memory = new char[sizeOf(type)*numItems1*numItems2*numItems3];
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
  if(auto obj = getObject(a)) {
    return obj->memory;
  } else {
    return nullptr;
  }
}

void SinkDevice::unmapArray(ANARIArray a)
{

}

// Renderable Objects /////////////////////////////////////////////////////////

ANARILight SinkDevice::newLight(const char *type)
{
  return nextHandle<ANARILight>();
}

ANARICamera SinkDevice::newCamera(const char *type)
{
  return nextHandle<ANARICamera>();
}

ANARIGeometry SinkDevice::newGeometry(const char *type)
{
  return nextHandle<ANARIGeometry>();
}

ANARISpatialField SinkDevice::newSpatialField(const char *type)
{
  return nextHandle<ANARISpatialField>();
}

ANARISurface SinkDevice::newSurface()
{
  return nextHandle<ANARISurface>();
}

ANARIVolume SinkDevice::newVolume(const char *type)
{
  return nextHandle<ANARIVolume>();
}

// Model Meta-Data ////////////////////////////////////////////////////////////

ANARIMaterial SinkDevice::newMaterial(const char *type)
{
  return nextHandle<ANARIMaterial>();
}

ANARISampler SinkDevice::newSampler(const char *type)
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

int SinkDevice::getProperty(ANARIObject object,
    const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    ANARIWaitMask mask)
{
  return 0;
}

// Object + Parameter Lifetime Management /////////////////////////////////////

struct FrameData {
  uint32_t width = 1;
  uint32_t height = 1;
};

void frame_deleter(void *userdata, void *memory) {
  delete[] static_cast<char*>(memory);
  delete static_cast<FrameData*>(userdata);
}

void SinkDevice::setParameter(
    ANARIObject object, const char *name, ANARIDataType type, const void *mem)
{
  if(auto obj = getObject(object)) {
    if(obj->type == ANARI_FRAME) {
      FrameData *data = static_cast<FrameData*>(obj->userdata);
      if(type == ANARI_UINT32_VEC2 && std::strncmp("size", name, 4)==0) {
        const uint32_t *size = static_cast<const uint32_t*>(mem);
        data->width = size[0];
        data->height = size[1];
        delete[] static_cast<char*>(obj->memory);
        obj->memory = nullptr;
      }
    }
  }
}

void SinkDevice::unsetParameter(ANARIObject object, const char *name)
{

}

void SinkDevice::commit(ANARIObject object)
{

}

void SinkDevice::release(ANARIObject object)
{
  if(auto obj = getObject(object)) {
    obj->release();
  }
}

void SinkDevice::retain(ANARIObject object)
{
  if(auto obj = getObject(object)) {
    obj->retain();
  }
}

// Frame Manipulation /////////////////////////////////////////////////////////

ANARIFrame SinkDevice::newFrame()
{
  ANARIFrame frame = nextHandle<ANARIFrame>();
  if(auto obj = getObject(frame)) {
    obj->userdata = new FrameData();
    obj->deleter = frame_deleter;
  }
  return frame;
}

const void *SinkDevice::frameBufferMap(ANARIFrame fb, const char *channel)
{
  if(auto obj = getObject(fb)) {
    if(obj->type == ANARI_FRAME) {
      FrameData *data = static_cast<FrameData*>(obj->userdata);
      if(obj->memory == nullptr) {
        obj->memory = new char[data->width*data->height*4*sizeof(float)];
      }
      return obj->memory;
    }
  }
  return nullptr;
}

void SinkDevice::frameBufferUnmap(ANARIFrame fb, const char *channel)
{

}

// Frame Rendering ////////////////////////////////////////////////////////////

ANARIRenderer SinkDevice::newRenderer(const char *type)
{
  return nextHandle<ANARIRenderer>();
}

void SinkDevice::renderFrame(ANARIFrame frame)
{

}

int SinkDevice::frameReady(ANARIFrame frame, ANARIWaitMask m)
{
  return 1;
}

void SinkDevice::discardFrame(ANARIFrame frame)
{

}

// Other SinkDevice definitions ////////////////////////////////////////////

SinkDevice::SinkDevice()
{
  nextHandle<ANARIObject>(); // insert a handle at 0
}

SinkDevice::~SinkDevice()
{

}

const char ** query_object_types(ANARIDataType type);
const ANARIParameter * query_params(ANARIDataType type, const char *subtype);

} // namespace sink_device
} // namespace anari

static char deviceName[] = "sink";

extern "C" SINK_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_NEW_DEVICE(
    sink, subtype)
{
  if (subtype == std::string("default") || subtype == std::string("sink"))
    return (ANARIDevice) new anari::sink_device::SinkDevice();
  return nullptr;
}

extern "C" SINK_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_INIT(sink)
{
  printf("...loaded sink library!\n");
}

extern "C" SINK_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_DEVICE_SUBTYPES(
    sink, libdata)
{
  static const char *devices[] = {deviceName, nullptr};
  return devices;
}

extern "C" SINK_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_OBJECT_SUBTYPES(
    sink, libdata, deviceSubtype, objectType)
{
  return anari::sink_device::query_object_types(objectType);
}

extern "C" SINK_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_OBJECT_PARAMETERS(
    sink, libdata, deviceSubtype, objectSubtype, objectType)
{
  return anari::sink_device::query_params(objectType, objectSubtype);
}

extern "C" SINK_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_PARAMETER_PROPERTY(
    sink,
    libdata,
    deviceSubtype,
    objectSubtype,
    objectType,
    parameterName,
    parameterType,
    propertyName,
    propertyType)
{
  return nullptr;
}

