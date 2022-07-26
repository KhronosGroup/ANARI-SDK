// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "DebugDevice.h"

#include "anari/anari.h"
#include "anari/backend/LibraryImpl.h"

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
    uint64_t numItems,
    uint64_t byteStride)
{
  ANARIArray1D handle;
  const void *forward = appMemory;
  uint64_t forwardStride = byteStride;

  if (isObject(type)) { // object arrays need special treatment
    const ANARIObject *in = static_cast<const ANARIObject *>(appMemory);
    ANARIObject *handles = new ANARIObject[numItems];
    if (byteStride != 0 && byteStride != sizeof(ANARIObject)) {
      reportStatus(this_device(),
          ANARI_DEVICE,
          ANARI_SEVERITY_ERROR,
          ANARI_STATUS_UNKNOWN_ERROR,
          "Strided Object arrays not supported in debug device");
      return 0; // strided handles not supported yet
    }

    if (appMemory != nullptr) {
      for (uint64_t i = 0; i < numItems; i++) {
        handles[i] = unwrapHandle(in[i]);
      }
      forward = handles;
      forwardStride = 0;
    }

    debug->anariNewArray1D(this_device(),
        appMemory,
        deleter,
        userData,
        type,
        numItems,
        uint64_t(0));

    DeleterWrapperData *deleterData =
        new DeleterWrapperData(userData, appMemory, deleter);
    handle = anariNewArray1D(wrapped,
        forward,
        deleterWrapper,
        (const void *)deleterData,
        type,
        numItems,
        uint64_t(0));
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
        numItems,
        byteStride);
    handle = anariNewArray1D(
        wrapped, appMemory, deleter, userData, type, numItems, byteStride);
    handle = newHandle(handle);
  }

  if (auto info = getDynamicObjectInfo<DebugObject<ANARI_ARRAY1D>>(handle)) {
    info->attachArray(appMemory, type, numItems, 1, 1, byteStride, 0, 0);
  }

  if (serializer) {
    serializer->anariNewArray1D(this_device(),
        appMemory,
        deleter,
        userData,
        type,
        numItems,
        forwardStride,
        handle);
  }

  return handle;
}

ANARIArray2D DebugDevice::newArray2D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t byteStride1,
    uint64_t byteStride2)
{
  debug->anariNewArray2D(this_device(),
      appMemory,
      deleter,
      userData,
      type,
      numItems1,
      numItems2,
      byteStride1,
      byteStride2);
  ANARIArray2D handle = anariNewArray2D(wrapped,
      appMemory,
      deleter,
      userData,
      type,
      numItems1,
      numItems2,
      byteStride1,
      byteStride2);
  handle = newHandle(handle);

  if (auto info = getDynamicObjectInfo<DebugObject<ANARI_ARRAY2D>>(handle)) {
    info->attachArray(
        appMemory, type, numItems1, numItems2, 1, byteStride1, byteStride2, 0);
  }

  if (serializer) {
    serializer->anariNewArray2D(this_device(),
        appMemory,
        deleter,
        userData,
        type,
        numItems1,
        numItems2,
        byteStride1,
        byteStride2,
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
    uint64_t numItems3,
    uint64_t byteStride1,
    uint64_t byteStride2,
    uint64_t byteStride3)
{
  debug->anariNewArray3D(this_device(),
      appMemory,
      deleter,
      userData,
      type,
      numItems1,
      numItems2,
      numItems3,
      byteStride1,
      byteStride2,
      byteStride3);
  ANARIArray3D handle = anariNewArray3D(wrapped,
      appMemory,
      deleter,
      userData,
      type,
      numItems1,
      numItems2,
      numItems3,
      byteStride1,
      byteStride2,
      byteStride3);
  handle = newHandle(handle);

  if (auto info = getDynamicObjectInfo<DebugObject<ANARI_ARRAY3D>>(handle)) {
    info->attachArray(appMemory,
        type,
        numItems1,
        numItems2,
        numItems3,
        byteStride1,
        byteStride2,
        byteStride3);
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
        byteStride1,
        byteStride2,
        byteStride3,
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
          info->referencedBy(a);
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

ANARIInstance DebugDevice::newInstance()
{
  debug->anariNewInstance(this_device());
  ANARIInstance handle = anariNewInstance(wrapped);
  ANARIInstance result = newHandle(handle);

  if (serializer) {
    serializer->anariNewInstance(this_device(), result);
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

int DebugDevice::getProperty(ANARIObject object,
    const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    ANARIWaitMask mask)
{
  if (handleIsDevice(object)) {
    return 0;
  }

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
  debug->anariMapFrame(this_device(), fb, channel);
  const void *mapped = anariMapFrame(
      wrapped, unwrapHandle(fb), channel, width, height, pixelType);

  if (serializer) {
    serializer->anariMapFrame(this_device(), fb, channel, mapped);
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

ObjectFactory *getDebugFactory();

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
    // reset to generic debug objects
    debugObjectFactory = getDebugFactory();
    if (wrapped) {
      anariRetain(wrapped, wrapped);
      anariCommitParameters(wrapped, wrapped);
      ObjectFactory *(*factory_fun)();
      if (anariGetProperty(wrapped,
              wrapped,
              "debugObjects",
              ANARI_FUNCTION_POINTER,
              &factory_fun,
              sizeof(factory_fun),
              ANARI_NO_WAIT)) {
        debugObjectFactory = factory_fun();
      } else {
        reportStatus(this_device(),
            ANARI_DEVICE,
            ANARI_SEVERITY_INFO,
            ANARI_STATUS_UNKNOWN_ERROR,
            "Device doesn't provide custom debug objects. Using core feature set.");
      }
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
  debugObjectFactory = getDebugFactory();
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
              ANARI_INFO_sourceFeature,
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
              objtype, objsubtype, ANARI_INFO_sourceFeature, ANARI_INT32)) {
    used_features[*feature] += 1;
  } else {
    unknown_feature_uses += 1;
  }
}

} // namespace debug_device
} // namespace anari

static char deviceName[] = "debug";

extern "C" DEBUG_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_NEW_DEVICE(
    debug, library, subtype)
{
  if (subtype == std::string("default") || subtype == std::string("debug"))
    return (ANARIDevice) new anari::debug_device::DebugDevice(library);
  return nullptr;
}

extern "C" DEBUG_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_INIT(debug) {}

extern "C" DEBUG_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_DEVICE_SUBTYPES(
    debug, library)
{
  (void)library;
  static const char *devices[] = {deviceName, nullptr};
  return devices;
}

extern "C" DEBUG_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_OBJECT_SUBTYPES(
    debug, library, deviceSubtype, objectType)
{
  (void)library;
  (void)deviceSubtype;
  (void)objectType;
  return nullptr;
}

extern "C" DEBUG_DEVICE_INTERFACE ANARI_DEFINE_LIBRARY_GET_PARAMETER_PROPERTY(
    debug,
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
  (void)objectSubtype;
  (void)objectType;
  (void)parameterName;
  (void)parameterType;
  (void)propertyName;
  (void)propertyType;
  return nullptr;
}
