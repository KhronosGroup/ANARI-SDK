// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "DebugDevice.h"

#include "anari/detail/Library.h"
#include "anari/anari.h"

#include "DebugBasics.h"

// std
#include <cstdarg>


namespace anari {
namespace debug_device {

///////////////////////////////////////////////////////////////////////////////
// Helper functions ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void DebugDevice::reportStatus(
  ANARIObject source,
  ANARIDataType sourceType,
  ANARIStatusSeverity severity,
  ANARIStatusCode code,
  const char *format, ...) {
    va_list arglist;
    va_list arglist_copy;
    va_start(arglist, format);
    va_copy(arglist_copy, arglist);
    int count = std::vsnprintf(nullptr, 0, format, arglist);
    va_end( arglist );

    last_status_message.resize(count+1);

    va_start(arglist_copy, format);
    std::vsnprintf(last_status_message.data(), count+1, format, arglist_copy);
    va_end( arglist_copy );

    if(ANARIStatusCallback statusCallback = defaultStatusCallback()) {
      statusCallback(defaultStatusCallbackUserPtr(), this_device(),
        source, sourceType, severity, code, last_status_message.data());
    }
  }

///////////////////////////////////////////////////////////////////////////////
// DebugDevice definitions //////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int DebugDevice::deviceImplements(const char *_extension)
{
  return 0;
}

void DebugDevice::deviceSetParameter(
    const char *_id, ANARIDataType type, const void *mem)
{
  std::string id = _id;
  if (id == "wrappedDevice" && type == ANARI_DEVICE) {
    if(staged) {
      anariRelease(staged, staged);
    }
    staged = *static_cast<const ANARIDevice*>(mem);
    if(staged) {
      anariRetain(staged, staged);
    }
  } else if(staged) {
    anariSetParameter(staged, staged, _id, type, mem);
  }
}

void DebugDevice::deviceUnsetParameter(const char *id)
{
  if(wrapped) {
    anariUnsetParameter(wrapped, wrapped, id);
  }
}

ObjectFactory* getDebugFactory();

void DebugDevice::deviceCommit()
{
  // skip this for now since thd debug device doesn't understand
  // device parameters and commits very well
  //debug->anariCommit(this_device(), this_device());

  if(wrapped != staged) {
    if(wrapped) {
      anariRelease(wrapped, wrapped);
    }
    wrapped = staged;
    // reset to generic debug objects
    debugObjectFactory = getDebugFactory();
    if(wrapped) {
      anariRetain(wrapped, wrapped);
      anariCommit(wrapped, wrapped);
      ObjectFactory* (*factory_fun)();
      if(anariGetProperty(wrapped, wrapped, "debugObjects", ANARI_FUNCTION_POINTER,
        &factory_fun, sizeof(factory_fun), ANARI_NO_WAIT)) {
        debugObjectFactory = factory_fun();
      } else {
        reportStatus(this_device(), ANARI_DEVICE,
          ANARI_SEVERITY_INFO, ANARI_STATUS_UNKNOWN_ERROR,
          "Device doesn't provide custom debug objects. Using core feature set.");
      }
    }
  }
}

void DebugDevice::deviceRetain()
{
  this->refInc();
}

void DebugDevice::deviceRelease()
{
  this->refDec();
}

// Data Arrays ////////////////////////////////////////////////////////////////

struct DeleterWrapperData {
  DeleterWrapperData(void *u, void *m, ANARIMemoryDeleter d)
    : userData(u), memory(m), deleter(d) { }
  void *userData;
  void *memory;
  ANARIMemoryDeleter deleter;
};
void deleterWrapper(void *userData, void *memory) {
  if(userData) {
    DeleterWrapperData *nested = static_cast<DeleterWrapperData*>(userData);
    if(nested->deleter) {
      nested->deleter(nested->userData, nested->memory);
    }
    delete nested;
  }
  delete[] static_cast<ANARIObject*>(memory);
}

ANARIArray1D DebugDevice::newArray1D(void *appMemory,
    ANARIMemoryDeleter deleter,
    void *userData,
    ANARIDataType type,
    uint64_t numItems,
    uint64_t byteStride)
{
  ANARIArray1D handle;
  if(isObject(type)) { // object arrays need special treatment
    ANARIObject *in = static_cast<ANARIObject*>(appMemory);
    ANARIObject *handles = new ANARIObject[numItems];
    if(byteStride != 0 && byteStride != sizeof(ANARIObject)) {
      reportStatus(this_device(), ANARI_DEVICE, ANARI_SEVERITY_ERROR, ANARI_STATUS_UNKNOWN_ERROR, "Strided Object arrays not supported in debug device");
      return 0; //strided handles not supported yet
    }

    void *forward = nullptr;
    if(appMemory != nullptr) {
      for(int i = 0;i<numItems;i++) {
        handles[i] = unwrapHandle(in[i]);
      }
      forward = handles;
    }

    debug->anariNewArray1D(this_device(), appMemory, deleter, userData, type, numItems, uint64_t(0));

    DeleterWrapperData *deleterData = new DeleterWrapperData(userData, appMemory, deleter);
    handle = anariNewArray1D(wrapped, forward, deleterWrapper, (void*)deleterData, type, numItems, uint64_t(0));
    handle = newHandle(handle);

    if(auto info = getDynamicObjectInfo<ArrayDebugObject<ANARI_ARRAY1D>>(handle)) {
      info->handles = handles;
      if(appMemory != nullptr) {
        for(int i = 0;i<numItems;i++) {
          if(auto info2 = getObjectInfo(in[i])) {
            info2->referencedBy(handle);
          }
        }
      }
    }
  } else {
    debug->anariNewArray1D(this_device(), appMemory, deleter, userData, type, numItems, byteStride);
    handle = anariNewArray1D(wrapped, appMemory, deleter, userData, type, numItems, byteStride);
    handle = newHandle(handle);
  }

  if(auto info = getDynamicObjectInfo<ArrayDebugObject<ANARI_ARRAY1D>>(handle)) {
    info->attachArray(appMemory, type,
    numItems, 1, 1,
    byteStride, 0, 0);
  }

  return handle;
}

ANARIArray2D DebugDevice::newArray2D(void *appMemory,
    ANARIMemoryDeleter deleter,
    void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t byteStride1,
    uint64_t byteStride2)
{
  debug->anariNewArray2D(this_device(), appMemory, deleter, userData, type, numItems1, numItems2, byteStride1, byteStride2);
  ANARIArray2D handle = anariNewArray2D(wrapped, appMemory, deleter, userData, type, numItems1, numItems2, byteStride1, byteStride2);
  handle = newHandle(handle);

  if(auto info = getDynamicObjectInfo<ArrayDebugObject<ANARI_ARRAY2D>>(handle)) {
    info->attachArray(appMemory, type,
    numItems1, numItems2, 1,
    byteStride1, byteStride2, 0);
  }
  return handle;
}

ANARIArray3D DebugDevice::newArray3D(void *appMemory,
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
  debug->anariNewArray3D(this_device(), appMemory, deleter, userData, type, numItems1, numItems2, numItems3, byteStride1, byteStride2, byteStride3);
  ANARIArray3D handle = anariNewArray3D(wrapped, appMemory, deleter, userData, type, numItems1, numItems2, numItems3, byteStride1, byteStride2, byteStride3);
  handle = newHandle(handle);

  if(auto info = getDynamicObjectInfo<ArrayDebugObject<ANARI_ARRAY3D>>(handle)) {
    info->attachArray(appMemory, type,
    numItems1, numItems2, numItems3,
    byteStride1, byteStride2, byteStride3);
  }
  return handle;
}

void *DebugDevice::mapArray(ANARIArray a)
{
  debug->anariMapArray(this_device(), a);
  void *result = anariMapArray(wrapped, unwrapHandle(a));

  if(auto info = getDynamicObjectInfo<GenericArrayDebugObject>(a)) {
    info->mapArray(result);
    if(isObject(info->arrayType)) {
      return info->handles;
    } else {
      return result;
    }
  } else {
    return nullptr;
  }
}

void DebugDevice::unmapArray(ANARIArray a)
{

  if(auto info = getDynamicObjectInfo<GenericArrayDebugObject>(a)) {

    if(isObject(info->arrayType)) {
      ANARIObject *objMapping = (ANARIObject*)info->mapping;
      // translate handles before unmapping
      for(int i = 0;i<info->numItems1;i++) {
        objMapping[i] = unwrapHandle(info->handles[i]);
        if(auto info2 = getObjectInfo(info->handles[i])) {
          info->referencedBy(a);
        }
      }
    }
    info->unmapArray();
  }

  debug->anariUnmapArray(this_device(), a);
  anariUnmapArray(wrapped, unwrapHandle(a));
}

// Renderable Objects /////////////////////////////////////////////////////////

ANARILight DebugDevice::newLight(const char *type)
{
  debug->anariNewLight(this_device(), type);
  ANARILight handle = anariNewLight(wrapped, type);
  return newHandle(handle, type);
}

ANARICamera DebugDevice::newCamera(const char *type)
{
  debug->anariNewCamera(this_device(), type);
  ANARICamera handle = anariNewCamera(wrapped, type);
  return newHandle(handle, type);
}

ANARIGeometry DebugDevice::newGeometry(const char *type)
{
  debug->anariNewGeometry(this_device(), type);
  ANARIGeometry handle = anariNewGeometry(wrapped, type);
  return newHandle(handle, type);
}

ANARISpatialField DebugDevice::newSpatialField(const char *type)
{
  debug->anariNewSpatialField(this_device(), type);
  ANARISpatialField handle = anariNewSpatialField(wrapped, type);
  return newHandle(handle, type);
}

ANARISurface DebugDevice::newSurface()
{
  debug->anariNewSurface(this_device());
  ANARISurface handle = anariNewSurface(wrapped);
  return newHandle(handle);
}

ANARIVolume DebugDevice::newVolume(const char *type)
{
  debug->anariNewVolume(this_device(), type);
  ANARIVolume handle = anariNewVolume(wrapped, type);
  return newHandle(handle, type);
}

// Model Meta-Data ////////////////////////////////////////////////////////////

ANARIMaterial DebugDevice::newMaterial(const char *type)
{
  debug->anariNewMaterial(this_device(), type);
  ANARIMaterial handle = anariNewMaterial(wrapped, type);
  return newHandle(handle, type);
}

ANARISampler DebugDevice::newSampler(const char *type)
{
  debug->anariNewSampler(this_device(), type);
  ANARISampler handle = anariNewSampler(wrapped, type);
  return newHandle(handle, type);
}

// Instancing /////////////////////////////////////////////////////////////////

ANARIGroup DebugDevice::newGroup()
{
  debug->anariNewGroup(this_device());
  ANARIGroup handle = anariNewGroup(wrapped);
  return newHandle(handle);
}

ANARIInstance DebugDevice::newInstance()
{
  debug->anariNewInstance(this_device());
  ANARIInstance handle = anariNewInstance(wrapped);
  return newHandle(handle);
}

// Top-level Worlds ///////////////////////////////////////////////////////////

ANARIWorld DebugDevice::newWorld()
{
  debug->anariNewWorld(this_device());
  ANARIWorld handle = anariNewWorld(wrapped);
  return newHandle(handle);
}

int DebugDevice::getProperty(ANARIObject object,
    const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    ANARIWaitMask mask)
{
  debug->anariGetProperty(this_device(), object, name, type, mem, size, mask);
  return anariGetProperty(wrapped, unwrapHandle(object), name, type, mem, size, mask);
}

// Object + Parameter Lifetime Management /////////////////////////////////////

void DebugDevice::setParameter(
    ANARIObject object, const char *name, ANARIDataType type, const void *mem)
{
  const void *unwrapped = mem;
  // translate object as parameter
  ANARIObject obj = nullptr;
  if(isObject(type)) {
    ANARIObject handle = *static_cast<const ANARIObject*>(mem);
    if(auto info = getObjectInfo(handle)) {
      info->referencedBy(object);
    }
    obj = unwrapHandle(handle);
    unwrapped = &obj;
  }

  debug->anariSetParameter(this_device(), object, name, type, mem);
  anariSetParameter(wrapped, unwrapHandle(object), name, type, unwrapped);

  if(auto info = getObjectInfo(object)) {
    info->setParameter(name, type, mem);
  }
}

void DebugDevice::unsetParameter(ANARIObject object, const char *name)
{
  debug->anariUnsetParameter(this_device(), object, name);
  anariUnsetParameter(wrapped, unwrapHandle(object), name);

  if(auto info = getObjectInfo(object)) {
    info->unsetParameter(name);
  }
}

void DebugDevice::commit(ANARIObject object)
{

  debug->anariCommit(this_device(), object);
  anariCommit(wrapped, unwrapHandle(object));

  if(auto info = getObjectInfo(object)) {
    info->commit();
  }
}

void DebugDevice::release(ANARIObject object)
{
  debug->anariRelease(this_device(), object);
  anariRelease(wrapped, unwrapHandle(object));

  if(auto info = getObjectInfo(object)) {
    info->release();
  }
}

void DebugDevice::retain(ANARIObject object)
{
  debug->anariRetain(this_device(), object);
  anariRetain(wrapped, unwrapHandle(object));

  if(auto info = getObjectInfo(object)) {
    info->retain();
  }
}

// Frame Manipulation /////////////////////////////////////////////////////////

ANARIFrame DebugDevice::newFrame()
{
  debug->anariNewFrame(this_device());
  ANARIFrame handle = anariNewFrame(wrapped);
  return newHandle(handle);
}

const void *DebugDevice::frameBufferMap(ANARIFrame fb, const char *channel)
{
  debug->anariMapFrame(this_device(), fb, channel);
  return anariMapFrame(wrapped, unwrapHandle(fb), channel);
}

void DebugDevice::frameBufferUnmap(ANARIFrame fb, const char *channel)
{
  debug->anariUnmapFrame(this_device(), fb, channel);
  anariUnmapFrame(wrapped, unwrapHandle(fb), channel);
}

// Frame Rendering ////////////////////////////////////////////////////////////

ANARIRenderer DebugDevice::newRenderer(const char *type)
{
  debug->anariNewRenderer(this_device(), type);
  ANARIRenderer handle = anariNewRenderer(wrapped, type);
  return newHandle(handle, type);
}

void DebugDevice::renderFrame(ANARIFrame frame)
{
  debug->anariRenderFrame(this_device(), frame);
  anariRenderFrame(wrapped, unwrapHandle(frame));

  if(auto info = getObjectInfo(frame)) {
    info->used();
  }
}

int DebugDevice::frameReady(ANARIFrame frame, ANARIWaitMask m)
{
  debug->anariFrameReady(this_device(), frame, m);
  return anariFrameReady(wrapped, unwrapHandle(frame), m);
}

void DebugDevice::discardFrame(ANARIFrame frame)
{
  debug->anariDiscardFrame(this_device(), frame);
  anariDiscardFrame(wrapped, unwrapHandle(frame));
}

// Other DebugDevice definitions ////////////////////////////////////////////

DebugDevice::DebugDevice() : wrapped(nullptr), staged(nullptr), debugObjectFactory(nullptr), deviceInfo{this, this_device(), this_device()}
{
  // insert the null handle explicitly as that always translates to null
  objectMap[nullptr] = nullptr;
  objects.emplace_back(new GenericDebugObject{});
  objects[0]->setName("Null Object");

  debug.reset(new DebugBasics(this));
  debugObjectFactory = getDebugFactory();
}

DebugDevice::~DebugDevice()
{
  if(debug) {
    debug->anariReleaseDevice(this_device());
  }
  if(wrapped) {
    anariRelease(wrapped, wrapped);
  }
}

ANARIObject DebugDevice::newObjectHandle(ANARIObject h, ANARIDataType type, const char *name) {
  ANARIObject idx = (ANARIObject)objects.size();
  objects.emplace_back(debugObjectFactory->new_by_subtype(type, name, this, idx, h));
  objectMap[h] = idx;
  return idx;
}
ANARIObject DebugDevice::newObjectHandle(ANARIObject h, ANARIDataType type) {
  ANARIObject idx = (ANARIObject)objects.size();
  objects.emplace_back(debugObjectFactory->new_by_type(type, this, idx, h));
  objectMap[h] = idx;
  return idx;
}
ANARIObject DebugDevice::wrapObjectHandle(ANARIObject h, ANARIDataType type) {
  if(h == wrapped) {
    return this_device();
  } else {
    auto iter = objectMap.find(h);
    if(iter != objectMap.end()) {
      return objectMap[h];
    } else {

      return 0;
    }
  }
}
ANARIObject DebugDevice::unwrapObjectHandle(ANARIObject h, ANARIDataType type) {
  if(h == this_device()) {
    return wrapped;
  } else if((uintptr_t)h < objects.size()) {
    return objects[(uintptr_t)h]->getHandle();
  } else {
    return nullptr;
  }
}
DebugObjectBase* DebugDevice::getObjectInfo(ANARIObject h) {
  if(h == this_device()) {
    return &deviceInfo;
  } else if((uintptr_t)h < objects.size()) {
    return objects[(uintptr_t)h].get();
  } else {
    return nullptr;
  }
}

} // namespace debug_device
} // namespace anari

static char deviceName[] = "debug";

extern "C" DEBUG_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_NEW_DEVICE(
    debug, subtype)
{
  if (subtype == std::string("default") || subtype == std::string("debug"))
    return (ANARIDevice) new anari::debug_device::DebugDevice();
  return nullptr;
}

extern "C" DEBUG_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_INIT(debug)
{
  printf("...loaded debug library!\n");
}

extern "C" DEBUG_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_DEVICE_SUBTYPES(
    debug, libdata)
{
  static const char *devices[] = {deviceName, nullptr};
  return devices;
}

extern "C" DEBUG_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_OBJECT_SUBTYPES(
    debug, libdata, deviceSubtype, objectType)
{
  return nullptr;
}

extern "C" DEBUG_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_OBJECT_PARAMETERS(
    debug, libdata, deviceSubtype, objectSubtype, objectType)
{
  return nullptr;
}

extern "C" DEBUG_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_PARAMETER_PROPERTY(
    debug,
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

