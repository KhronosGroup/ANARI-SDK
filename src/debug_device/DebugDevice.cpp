// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "DebugDevice.h"

#include "anari/anari.h"

#include "CodeSerializer.h"
#include "DebugBasics.h"
#include "EmptySerializer.h"

// std
#include <cstdarg>

namespace anari {
namespace debug_device {

///////////////////////////////////////////////////////////////////////////////
// Helper functions ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void DebugDevice::reportStatus(ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *format,
    ...)
{
  va_list arglist;
  va_list arglist_copy;
  va_start(arglist, format);
  va_copy(arglist_copy, arglist);
  int count = std::vsnprintf(nullptr, 0, format, arglist);
  va_end(arglist);

  last_status_message.resize(size_t(count + 1));

  std::vsnprintf(
      last_status_message.data(), size_t(count + 1), format, arglist_copy);
  va_end(arglist_copy);

  if (ANARIStatusCallback statusCallback = defaultStatusCallback()) {
    statusCallback(defaultStatusCallbackUserPtr(),
        this_device(),
        source,
        sourceType,
        severity,
        code,
        last_status_message.data());
  }
  if (serializer) {
    serializer->insertStatus(
        source, sourceType, severity, code, last_status_message.data());
  }
}

void DebugDevice::vreportStatus(ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *format,
    va_list arglist)
{
  va_list arglist_copy;
  va_copy(arglist_copy, arglist);
  int count = std::vsnprintf(nullptr, 0, format, arglist);

  last_status_message.resize(size_t(count + 1));

  std::vsnprintf(
      last_status_message.data(), size_t(count + 1), format, arglist_copy);
  va_end(arglist_copy);

  if (ANARIStatusCallback statusCallback = defaultStatusCallback()) {
    statusCallback(defaultStatusCallbackUserPtr(),
        this_device(),
        source,
        sourceType,
        severity,
        code,
        last_status_message.data());
  }
  if (serializer) {
    serializer->insertStatus(
        source, sourceType, severity, code, last_status_message.data());
  }
}
///////////////////////////////////////////////////////////////////////////////
// DebugDevice definitions //////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// Data Arrays ////////////////////////////////////////////////////////////////

struct DeleterWrapperData
{
  DeleterWrapperData(const void *u, const void *m, ANARIMemoryDeleter d)
      : userData(u), memory(m), deleter(d)
  {}
  const void *userData;
  const void *memory;
  ANARIMemoryDeleter deleter;
};
void deleterWrapper(const void *userData, const void *memory)
{
  if (userData) {
    const DeleterWrapperData *nested =
        static_cast<const DeleterWrapperData *>(userData);
    if (nested->deleter) {
      nested->deleter(nested->userData, nested->memory);
    }
    delete nested;
  }
  delete[] static_cast<const ANARIObject *>(const_cast<void *>(memory));
}

ANARIArray1D DebugDevice::newArray1D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems)
{
  ANARIArray1D handle;
  const void *forward = appMemory;

  if (isObject(type)) { // object arrays need special treatment
    const ANARIObject *in = static_cast<const ANARIObject *>(appMemory);
    ANARIObject *handles = new ANARIObject[numItems]();

    if (appMemory != nullptr) {
      for (uint64_t i = 0; i < numItems; i++) {
        handles[i] = unwrapHandle(in[i]);
      }
      forward = handles;
    }

    debug->anariNewArray1D(this_device(),
        appMemory,
        deleter,
        userData,
        type,
        numItems);

    DeleterWrapperData *deleterData =
        new DeleterWrapperData(userData, appMemory, deleter);
    handle = anariNewArray1D(wrapped,
        forward,
        deleterWrapper,
        (const void *)deleterData,
        type,
        numItems);
    handle = newHandle(handle);

    if (auto info = getDynamicObjectInfo<DebugObject<ANARI_ARRAY1D>>(handle)) {
      info->handles = handles;
      if (appMemory != nullptr) {
        for (uint64_t i = 0; i < numItems; i++) {
          if (auto info2 = getObjectInfo(in[i])) {
            info2->referencedBy(handle);
          }
        }
      }
    }
  } else {
    debug->anariNewArray1D(this_device(),
        appMemory,
        deleter,
        userData,
        type,
        numItems);
    handle = anariNewArray1D(
        wrapped, appMemory, deleter, userData, type, numItems);
    handle = newHandle(handle);
  }

  if (auto info = getDynamicObjectInfo<DebugObject<ANARI_ARRAY1D>>(handle)) {
    info->attachArray(appMemory, type, numItems, 1, 1, 0, 0, 0);
  }

  if (serializer) {
    serializer->anariNewArray1D(this_device(),
        appMemory,
        deleter,
        userData,
        type,
        numItems,
        handle);
  }

  return handle;
}

ANARIArray2D DebugDevice::newArray2D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2)
{
  debug->anariNewArray2D(this_device(),
      appMemory,
      deleter,
      userData,
      type,
      numItems1,
      numItems2);
  ANARIArray2D handle = anariNewArray2D(wrapped,
      appMemory,
      deleter,
      userData,
      type,
      numItems1,
      numItems2);
  handle = newHandle(handle);

  if (auto info = getDynamicObjectInfo<DebugObject<ANARI_ARRAY2D>>(handle)) {
    info->attachArray(
        appMemory, type, numItems1, numItems2, 1, 0, 0, 0);
  }

  if (serializer) {
    serializer->anariNewArray2D(this_device(),
        appMemory,
        deleter,
        userData,
        type,
        numItems1,
        numItems2,
        handle);
  }

  return handle;
}

ANARIArray3D DebugDevice::newArray3D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3)
{
  debug->anariNewArray3D(this_device(),
      appMemory,
      deleter,
      userData,
      type,
      numItems1,
      numItems2,
      numItems3);
  ANARIArray3D handle = anariNewArray3D(wrapped,
      appMemory,
      deleter,
      userData,
      type,
      numItems1,
      numItems2,
      numItems3);
  handle = newHandle(handle);

  if (auto info = getDynamicObjectInfo<DebugObject<ANARI_ARRAY3D>>(handle)) {
    info->attachArray(appMemory,
        type,
        numItems1,
        numItems2,
        numItems3,
        0, 0, 0);
  }

  if (serializer) {
    serializer->anariNewArray3D(this_device(),
        appMemory,
        deleter,
        userData,
        type,
        numItems1,
        numItems2,
        numItems3,
        handle);
  }

  return handle;
}

void *DebugDevice::mapArray(ANARIArray a)
{
  debug->anariMapArray(this_device(), a);
  void *ptr = anariMapArray(wrapped, unwrapHandle(a));

  void *result = nullptr;
  if (auto info = getDynamicObjectInfo<GenericArrayDebugObject>(a)) {
    info->mapArray(ptr);
    if (isObject(info->arrayType)) {
      result = info->handles;
    } else {
      result = ptr;
    }
  }

  if (serializer) {
    serializer->anariMapArray(this_device(), a, result);
  }

  return result;
}

void DebugDevice::unmapArray(ANARIArray a)
{
  auto info = getDynamicObjectInfo<GenericArrayDebugObject>(a);
  if (info) {
    if (isObject(info->arrayType)) {
      ANARIObject *objMapping = (ANARIObject *)info->mapping;
      // translate handles before unmapping
      for (uint64_t i = 0; i < info->numItems1; i++) {
        objMapping[i] = unwrapHandle(info->handles[i]);
        if (auto info2 = getObjectInfo(info->handles[i])) {
          info2->referencedBy(a);
        }
      }
    }
  }

  debug->anariUnmapArray(this_device(), a);
  anariUnmapArray(wrapped, unwrapHandle(a));

  if (serializer) {
    serializer->anariUnmapArray(this_device(), a);
  }
  // the serializer may also need to inspect the array
  if (info) {
    info->unmapArray();
  }
}

// Renderable Objects /////////////////////////////////////////////////////////

ANARILight DebugDevice::newLight(const char *type)
{
  debug->anariNewLight(this_device(), type);
  ANARILight handle = anariNewLight(wrapped, type);
  ANARILight result = newHandle(handle, type);

  if (serializer) {
    serializer->anariNewLight(this_device(), type, result);
  }

  return result;
}

ANARICamera DebugDevice::newCamera(const char *type)
{
  debug->anariNewCamera(this_device(), type);
  ANARICamera handle = anariNewCamera(wrapped, type);
  ANARICamera result = newHandle(handle, type);

  if (serializer) {
    serializer->anariNewCamera(this_device(), type, result);
  }

  return result;
}

ANARIGeometry DebugDevice::newGeometry(const char *type)
{
  debug->anariNewGeometry(this_device(), type);
  ANARIGeometry handle = anariNewGeometry(wrapped, type);
  ANARIGeometry result = newHandle(handle, type);

  if (serializer) {
    serializer->anariNewGeometry(this_device(), type, result);
  }

  return result;
}

ANARISpatialField DebugDevice::newSpatialField(const char *type)
{
  debug->anariNewSpatialField(this_device(), type);
  ANARISpatialField handle = anariNewSpatialField(wrapped, type);
  ANARISpatialField result = newHandle(handle, type);

  if (serializer) {
    serializer->anariNewSpatialField(this_device(), type, result);
  }

  return result;
}

ANARISurface DebugDevice::newSurface()
{
  debug->anariNewSurface(this_device());
  ANARISurface handle = anariNewSurface(wrapped);
  ANARISurface result = newHandle(handle);

  if (serializer) {
    serializer->anariNewSurface(this_device(), result);
  }

  return result;
}

ANARIVolume DebugDevice::newVolume(const char *type)
{
  debug->anariNewVolume(this_device(), type);
  ANARIVolume handle = anariNewVolume(wrapped, type);
  ANARIVolume result = newHandle(handle, type);

  if (serializer) {
    serializer->anariNewVolume(this_device(), type, result);
  }

  return result;
}

// Model Meta-Data ////////////////////////////////////////////////////////////

ANARIMaterial DebugDevice::newMaterial(const char *type)
{
  debug->anariNewMaterial(this_device(), type);
  ANARIMaterial handle = anariNewMaterial(wrapped, type);
  ANARIMaterial result = newHandle(handle, type);

  if (serializer) {
    serializer->anariNewMaterial(this_device(), type, result);
  }

  return result;
}

ANARISampler DebugDevice::newSampler(const char *type)
{
  debug->anariNewSampler(this_device(), type);
  ANARISampler handle = anariNewSampler(wrapped, type);
  ANARISampler result = newHandle(handle, type);

  if (serializer) {
    serializer->anariNewSampler(this_device(), type, result);
  }

  return result;
}

// Instancing /////////////////////////////////////////////////////////////////

ANARIGroup DebugDevice::newGroup()
{
  debug->anariNewGroup(this_device());
  ANARIGroup handle = anariNewGroup(wrapped);
  ANARIGroup result = newHandle(handle);

  if (serializer) {
    serializer->anariNewGroup(this_device(), result);
  }

  return result;
}

ANARIInstance DebugDevice::newInstance(const char *type)
{
  debug->anariNewInstance(this_device(), type);
  ANARIInstance handle = anariNewInstance(wrapped, type);
  ANARIInstance result = newHandle(handle);

  if (serializer) {
    serializer->anariNewInstance(this_device(), type, result);
  }

  return result;
}

// Top-level Worlds ///////////////////////////////////////////////////////////

ANARIWorld DebugDevice::newWorld()
{
  debug->anariNewWorld(this_device());
  ANARIWorld handle = anariNewWorld(wrapped);
  ANARIWorld result = newHandle(handle);

  if (serializer) {
    serializer->anariNewWorld(this_device(), result);
  }

  return result;
}


// todo: maybe add serialization for these?
const char ** DebugDevice::getObjectSubtypes(ANARIDataType objectType)
{
  return anariGetObjectSubtypes(wrapped, objectType);
}

const void* DebugDevice::getObjectInfo(ANARIDataType objectType,
    const char* objectSubtype,
    const char* infoName,
    ANARIDataType infoType)
{
  return anariGetObjectInfo(wrapped, objectType, objectSubtype, infoName, infoType);
}

const void* DebugDevice::getParameterInfo(ANARIDataType objectType,
    const char* objectSubtype,
    const char* parameterName,
    ANARIDataType parameterType,
    const char* infoName,
    ANARIDataType infoType)
{
  return anariGetParameterInfo(wrapped,
      objectType,
      objectSubtype,
      parameterName,
      parameterType,
      infoName,
      infoType);
}


int DebugDevice::getProperty(ANARIObject object,
    const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    ANARIWaitMask mask)
{
  debug->anariGetProperty(this_device(), object, name, type, mem, size, mask);

  int result = anariGetProperty(
      wrapped, unwrapHandle(object), name, type, mem, size, mask);

  if (serializer) {
    serializer->anariGetProperty(
        this_device(), object, name, type, mem, size, mask, result);
  }

  return result;
}

// Object + Parameter Lifetime Management /////////////////////////////////////

void frameContinuationWrapper(
    const void *userdata, ANARIDevice, ANARIFrame frame)
{
  DebugDevice *dd =
      reinterpret_cast<DebugDevice *>(const_cast<void *>(userdata));
  ANARIFrame debugFrame = dd->wrapHandle(frame);
  DebugObject<ANARI_FRAME> *info =
      dd->getDynamicObjectInfo<DebugObject<ANARI_FRAME>>(debugFrame);
  info->frameContinuationFun(info->userdata, dd->this_device(), debugFrame);
}

void DebugDevice::setParameter(
    ANARIObject object, const char *name, ANARIDataType type, const void *mem)
{
  if (handleIsDevice(object)) {
    deviceSetParameter(name, type, mem);
    if (!wrapped) {
      return;
    }
  }

  const void *unwrapped = mem;
  // translate object as parameter
  ANARIObject obj = nullptr;
  if (isObject(type)) {
    ANARIObject handle = *static_cast<const ANARIObject *>(mem);
    if (auto info = getObjectInfo(handle)) {
      info->referencedBy(object);
    }
    obj = unwrapHandle(handle);
    unwrapped = &obj;
  }

  debug->anariSetParameter(this_device(), object, name, type, mem);

  // frame completion callbacks require special treatment
  if (type == ANARI_FRAME_COMPLETION_CALLBACK
      && std::strncmp(name, "frameCompletionCallback", 23) == 0) {
    ANARIFrameCompletionCallback callbackWrapper = frameContinuationWrapper;
    anariSetParameter(wrapped,
        unwrapHandle(object),
        "frameCompletionCallback",
        ANARI_FRAME_COMPLETION_CALLBACK,
        &callbackWrapper);
    anariSetParameter(wrapped,
        unwrapHandle(object),
        "frameCompletionCallbackUserData",
        ANARI_VOID_POINTER,
        this);
  } else if (type == ANARI_VOID_POINTER
      && std::strncmp(name, "frameCompletionCallbackUserData", 31) == 0) {
    // do not forward or this will overwrite the this pointer
  } else {
    anariSetParameter(wrapped, unwrapHandle(object), name, type, unwrapped);
  }

  if (serializer) {
    serializer->anariSetParameter(this_device(), object, name, type, mem);
  }

  if (auto info = getObjectInfo(object)) {
    info->setParameter(name, type, mem);
    reportParameterUse(info->getType(), info->getSubtype(), name, type);
  }
}

void DebugDevice::unsetParameter(ANARIObject object, const char *name)
{
  if (handleIsDevice(object))
    deviceUnsetParameter(name);
  else {
    debug->anariUnsetParameter(this_device(), object, name);
    anariUnsetParameter(wrapped, unwrapHandle(object), name);

    if (serializer) {
      serializer->anariUnsetParameter(this_device(), object, name);
    }

    if (auto info = getObjectInfo(object))
      info->unsetParameter(name);
  }
}

void DebugDevice::unsetAllParameters(ANARIObject object)
{
  if (handleIsDevice(object))
    deviceCommit();
  else {
    debug->anariUnsetAllParameters(this_device(), object);
    anariUnsetAllParameters(wrapped, unwrapHandle(object));

    if (auto info = getObjectInfo(object))
      info->unsetAllParameters();
  }

  if (serializer) {
    serializer->anariUnsetAllParameters(this_device(), object);
  }
}

void* DebugDevice::mapParameterArray1D(ANARIObject object,
    const char* name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t* elementStride)
{
  /*
  if (handleIsDevice(object)) {
    //??
  }
  */
  debug->anariMapParameterArray1D(this_device(), object, name, dataType, numElements1, elementStride);

  void *result = anariMapParameterArray1D(wrapped, unwrapHandle(object), name, dataType, numElements1, elementStride);

  if (auto info = getDynamicObjectInfo<GenericDebugObject>(object)) {
    info->mapParameter(name, dataType, numElements1, elementStride, result);
    reportParameterUse(info->getType(), info->getSubtype(), name, ANARI_ARRAY1D);

    if (serializer) {
      serializer->anariMapParameterArray1D(this_device(), object, name, dataType, numElements1, elementStride, result);
    }
  }

  return result;
}

void* DebugDevice::mapParameterArray2D(ANARIObject object,
    const char* name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t numElements2,
    uint64_t* elementStride)
{
  debug->anariMapParameterArray2D(this_device(), object, name, dataType, numElements1, numElements2, elementStride);

  void *result = anariMapParameterArray2D(wrapped, unwrapHandle(object), name, dataType, numElements1, numElements2, elementStride);

  if (auto info = getDynamicObjectInfo<GenericDebugObject>(object)) {
    info->mapParameter(name, dataType, numElements1*numElements2, elementStride, result);
    reportParameterUse(info->getType(), info->getSubtype(), name, ANARI_ARRAY2D);

    if (serializer) {
      serializer->anariMapParameterArray2D(this_device(), object, name, dataType, numElements1, numElements1, elementStride, result);
    }

  }

  return result;
}

void* DebugDevice::mapParameterArray3D(ANARIObject object,
    const char* name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t numElements2,
    uint64_t numElements3,
    uint64_t* elementStride)
{
  debug->anariMapParameterArray3D(this_device(), object, name, dataType, numElements1, numElements2, numElements3, elementStride);

  void *result = anariMapParameterArray3D(wrapped, unwrapHandle(object), name, dataType, numElements1, numElements2, numElements3, elementStride);

  if (auto info = getDynamicObjectInfo<GenericDebugObject>(object)) {
    info->mapParameter(name, dataType, numElements1*numElements2*numElements3, elementStride, result);
    reportParameterUse(info->getType(), info->getSubtype(), name, ANARI_ARRAY3D);

    if (serializer) {
      serializer->anariMapParameterArray3D(this_device(), object, name, dataType, numElements1, numElements2, numElements3, elementStride, result);
    }
  }

  return result;
}

void DebugDevice::unmapParameterArray(ANARIObject object,
    const char* name)
{
  debug->anariUnmapParameterArray(this_device(), object, name);

  if (serializer) {
    serializer->anariUnmapParameterArray(this_device(), object, name);
  }

  // this needs to retrieve the pointer and translate the handles
  if (auto info = getDynamicObjectInfo<GenericDebugObject>(object)) {
    ANARIDataType dataType;
    uint64_t elements;
    if(void *mem = info->getParameterMapping(name, dataType, elements)) {
      if(anari::isObject(dataType)) {
        ANARIObject *objects = static_cast<ANARIObject*>(mem);
        for(uint64_t i = 0;i<elements;++i) {
          if (auto info2 = getObjectInfo(objects[i])) {
            info2->referencedBy(object);
          }

          objects[i] = unwrapHandle(objects[i]);
        }
      }
      info->unmapParameter(name);
    }
  }

  anariUnmapParameterArray(wrapped, unwrapHandle(object), name);
}

void DebugDevice::commitParameters(ANARIObject object)
{
  if (handleIsDevice(object))
    deviceCommit();
  else {
    debug->anariCommitParameters(this_device(), object);
    anariCommitParameters(wrapped, unwrapHandle(object));

    if (auto info = getObjectInfo(object))
      info->commit();
  }

  if (serializer) {
    serializer->anariCommitParameters(this_device(), object);
  }
}

void DebugDevice::release(ANARIObject object)
{
  if (!object) {
    return;
  } else if (handleIsDevice(object)) {
    this->refDec();
  } else {
    debug->anariRelease(this_device(), object);
    anariRelease(wrapped, unwrapHandle(object));

    if (serializer) {
      serializer->anariRelease(this_device(), object);
    }

    if (auto info = getObjectInfo(object))
      info->release();
  }
}

void DebugDevice::retain(ANARIObject object)
{
  if (!object) {
    return;
  } else if (handleIsDevice(object)) {
    this->refInc();
  } else {
    debug->anariRetain(this_device(), object);
    anariRetain(wrapped, unwrapHandle(object));

    if (serializer) {
      serializer->anariRetain(this_device(), object);
    }

    if (auto info = getObjectInfo(object))
      info->retain();
  }
}

// Frame Manipulation /////////////////////////////////////////////////////////

ANARIFrame DebugDevice::newFrame()
{
  debug->anariNewFrame(this_device());
  ANARIFrame handle = anariNewFrame(wrapped);
  ANARIFrame result = newHandle(handle);

  if (serializer) {
    serializer->anariNewFrame(this_device(), result);
  }

  return result;
}

const void *DebugDevice::frameBufferMap(ANARIFrame fb,
    const char *channel,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *pixelType)
{
  debug->anariMapFrame(this_device(), fb, channel, width, height, pixelType);
  const void *mapped = anariMapFrame(
      wrapped, unwrapHandle(fb), channel, width, height, pixelType);

  if (serializer) {
    serializer->anariMapFrame(this_device(), fb, channel, width, height, pixelType, mapped);
  }

  return mapped;
}

void DebugDevice::frameBufferUnmap(ANARIFrame fb, const char *channel)
{
  debug->anariUnmapFrame(this_device(), fb, channel);
  anariUnmapFrame(wrapped, unwrapHandle(fb), channel);
  if (serializer) {
    serializer->anariUnmapFrame(this_device(), fb, channel);
  }
}

// Frame Rendering ////////////////////////////////////////////////////////////

ANARIRenderer DebugDevice::newRenderer(const char *type)
{
  debug->anariNewRenderer(this_device(), type);
  ANARIRenderer handle = anariNewRenderer(wrapped, type);
  ANARIRenderer result = newHandle(handle, type);

  if (serializer) {
    serializer->anariNewRenderer(this_device(), type, result);
  }

  return result;
}

void DebugDevice::renderFrame(ANARIFrame frame)
{
  debug->anariRenderFrame(this_device(), frame);
  anariRenderFrame(wrapped, unwrapHandle(frame));

  if (serializer) {
    serializer->anariRenderFrame(this_device(), frame);
  }

  if (auto info = getObjectInfo(frame)) {
    info->used();
  }
}

int DebugDevice::frameReady(ANARIFrame frame, ANARIWaitMask m)
{
  debug->anariFrameReady(this_device(), frame, m);
  int result = anariFrameReady(wrapped, unwrapHandle(frame), m);

  if (serializer) {
    serializer->anariFrameReady(this_device(), frame, m, result);
  }

  return result;
}

void DebugDevice::discardFrame(ANARIFrame frame)
{
  debug->anariDiscardFrame(this_device(), frame);
  anariDiscardFrame(wrapped, unwrapHandle(frame));

  if (serializer) {
    serializer->anariDiscardFrame(this_device(), frame);
  }
}

// Other DebugDevice definitions ////////////////////////////////////////////

void DebugDevice::deviceSetParameter(
    const char *_id, ANARIDataType type, const void *mem)
{
  std::string id = _id;
  if (id == "wrappedDevice" && type == ANARI_DEVICE) {
    if (staged) {
      anariRelease(staged, staged);
    }
    staged = *static_cast<const ANARIDevice *>(mem);
    if (staged) {
      anariRetain(staged, staged);
    }
  } else if (id == "traceMode" && type == ANARI_STRING) {
    std::string mode((const char *)mem);
    if (mode == "code") {
      createSerializer = CodeSerializer::create;
    }
  } else if (id == "traceDir" && type == ANARI_STRING) {
    traceDir = (const char *)mem;
  }
}

void DebugDevice::deviceUnsetParameter(const char *id)
{
  if (wrapped) {
    anariUnsetParameter(wrapped, wrapped, id);
  }
}

void DebugDevice::deviceCommit()
{
  // skip this for now since the debug device doesn't understand
  // device parameters and commits very well
  // debug->anariCommit(this_device(), this_device());

  if (wrapped != staged) {
    if (wrapped) {
      anariRelease(wrapped, wrapped);
    }
    wrapped = staged;
    if (wrapped) {
      anariRetain(wrapped, wrapped);
      anariCommitParameters(wrapped, wrapped);
    }
  }
  if (createSerializer) {
    serializer.reset(createSerializer(this));
    createSerializer = nullptr;
  }
}

DebugDevice::DebugDevice(ANARILibrary library)
    : DeviceImpl(library),
      wrapped(nullptr),
      staged(nullptr),
      deviceInfo{this, this_device(), this_device()},
      debugObjectFactory(nullptr)
{
  // insert the null handle explicitly as that always translates to null
  objectMap[nullptr] = nullptr;
  objects.emplace_back(new GenericDebugObject{});
  objects[0]->setName("Null Object");

  debug.reset(new DebugBasics(this));

  static ObjectFactory factory;
  debugObjectFactory = &factory;
}

DebugDevice::~DebugDevice()
{
  const char **features = anari::debug_queries::query_extensions();
  reportStatus(this_device(),
      ANARI_DEVICE,
      ANARI_SEVERITY_INFO,
      ANARI_STATUS_UNKNOWN_ERROR,
      "used features:");

  for (int i = 0; i < anari::debug_queries::extension_count; ++i) {
    if (used_features[i] > 0) {
      reportStatus(this_device(),
          ANARI_DEVICE,
          ANARI_SEVERITY_INFO,
          ANARI_STATUS_UNKNOWN_ERROR,
          "   %s",
          features[i]);
    }
  }

  debugObjectFactory->print_summary(this);
  if (debug) {
    debug->anariReleaseDevice(this_device());
  }
  if (wrapped) {
    anariRelease(wrapped, wrapped);
  }
}

ANARIObject DebugDevice::newObjectHandle(
    ANARIObject h, ANARIDataType type, const char *name)
{
  reportObjectUse(type, name);
  ANARIObject idx = (ANARIObject)objects.size();
  objects.emplace_back(
      debugObjectFactory->new_by_subtype(type, name, this, idx, h));
  objectMap[h] = idx;
  return idx;
}
ANARIObject DebugDevice::newObjectHandle(ANARIObject h, ANARIDataType type)
{
  reportObjectUse(type, "");
  ANARIObject idx = (ANARIObject)objects.size();
  objects.emplace_back(debugObjectFactory->new_by_type(type, this, idx, h));
  objectMap[h] = idx;
  return idx;
}
ANARIObject DebugDevice::wrapObjectHandle(ANARIObject h, ANARIDataType type)
{
  (void)type;
  if (h == wrapped) {
    return this_device();
  } else {
    auto iter = objectMap.find(h);
    if (iter != objectMap.end()) {
      return iter->second;
    } else {
      return 0;
    }
  }
}
ANARIObject DebugDevice::unwrapObjectHandle(ANARIObject h, ANARIDataType type)
{
  (void)type;
  if (h == this_device()) {
    return wrapped;
  } else if ((uintptr_t)h < objects.size()) {
    return objects[(uintptr_t)h]->getHandle();
  } else {
    return nullptr;
  }
}
DebugObjectBase *DebugDevice::getObjectInfo(ANARIObject h)
{
  if (h == this_device()) {
    return &deviceInfo;
  } else if ((uintptr_t)h < objects.size()) {
    return objects[(uintptr_t)h].get();
  } else {
    return nullptr;
  }
}

void DebugDevice::reportParameterUse(ANARIDataType objtype,
    const char *objsubtype,
    const char *paramname,
    ANARIDataType paramtype)
{
  if (const int32_t *feature =
          (const int32_t *)debug_queries::query_param_info_enum(objtype,
              objsubtype,
              paramname,
              paramtype,
              ANARI_INFO_sourceExtension,
              ANARI_INT32)) {
    used_features[*feature] += 1;
  } else {
    unknown_feature_uses += 1;
  }
}

void DebugDevice::reportObjectUse(ANARIDataType objtype, const char *objsubtype)
{
  if (const int32_t *feature =
          (const int32_t *)debug_queries::query_object_info_enum(
              objtype, objsubtype, ANARI_INFO_sourceExtension, ANARI_INT32)) {
    used_features[*feature] += 1;
  } else {
    unknown_feature_uses += 1;
  }
}

} // namespace debug_device
} // namespace anari
