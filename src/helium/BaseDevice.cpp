// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "BaseDevice.h"
#include "BaseFrame.h"
#include "array/Array.h"
// anari
#include "anari/backend/LibraryImpl.h"

namespace helium {

// Data Arrays ////////////////////////////////////////////////////////////////

void *BaseDevice::mapArray(ANARIArray a)
{
  auto lock = getObjectLock(a);
  return referenceFromHandle<BaseArray>(a).map();
}

void BaseDevice::unmapArray(ANARIArray a)
{
  auto lock = getObjectLock(a);
  referenceFromHandle<BaseArray>(a).unmap();
}

// Object + Parameter Lifetime Management /////////////////////////////////////

int BaseDevice::getProperty(ANARIObject object,
    const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    uint32_t mask)
{
  if (!handleIsDevice(object)) {
    if (mask == ANARI_WAIT)
      m_state->commitBuffer.flush();
    auto lock = getObjectLock(object);
    return referenceFromHandle(object).getProperty(name, type, mem, mask);
  } else
    return deviceGetProperty(name, type, mem, mask);

  return 0;
}

// Object + Parameter Lifetime Management /////////////////////////////////////

void BaseDevice::setParameter(
    ANARIObject object, const char *name, ANARIDataType type, const void *mem)
{
  auto lock = getObjectLock(object);

  if (handleIsDevice(object)) {
    deviceSetParameter(name, type, mem);
    return;
  }

  bool valueChanged = false;
  auto &o = referenceFromHandle(object);
  if (anari::isObject(type) && mem == nullptr)
    valueChanged = o.removeParam(name);
  else
    valueChanged = o.setParam(name, type, mem);

  if (valueChanged)
    o.markParameterChanged();
}

void BaseDevice::unsetParameter(ANARIObject o, const char *name)
{
  auto lock = getObjectLock(o);

  if (handleIsDevice(o))
    deviceUnsetParameter(name);
  else {
    auto &obj = referenceFromHandle(o);
    if (obj.removeParam(name))
      obj.markParameterChanged();
  }
}

void BaseDevice::unsetAllParameters(ANARIObject o)
{
  auto lock = getObjectLock(o);

  if (handleIsDevice(o))
    deviceUnsetAllParameters();
  else {
    auto &obj = referenceFromHandle(o);
    if (obj.removeAllParams())
      obj.markParameterChanged();
  }
}

void *BaseDevice::mapParameterArray1D(ANARIObject o,
    const char *name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t *elementStride)
{
  auto array = newArray1D(nullptr, nullptr, nullptr, dataType, numElements1);
  setParameter(o, name, ANARI_ARRAY1D, &array);
  *elementStride = anari::sizeOf(dataType);
  referenceFromHandle(array).refDec(RefType::PUBLIC);
  return mapArray(array);
}

void *BaseDevice::mapParameterArray2D(ANARIObject o,
    const char *name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t numElements2,
    uint64_t *elementStride)
{
  auto array = newArray2D(
      nullptr, nullptr, nullptr, dataType, numElements1, numElements2);
  setParameter(o, name, ANARI_ARRAY2D, &array);
  *elementStride = anari::sizeOf(dataType);
  referenceFromHandle(array).refDec(RefType::PUBLIC);
  return mapArray(array);
}

void *BaseDevice::mapParameterArray3D(ANARIObject o,
    const char *name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t numElements2,
    uint64_t numElements3,
    uint64_t *elementStride)
{
  auto array = newArray3D(nullptr,
      nullptr,
      nullptr,
      dataType,
      numElements1,
      numElements2,
      numElements3);
  setParameter(o, name, ANARI_ARRAY3D, &array);
  *elementStride = anari::sizeOf(dataType);
  referenceFromHandle(array).refDec(RefType::PUBLIC);
  return mapArray(array);
}

void BaseDevice::unmapParameterArray(ANARIObject o, const char *name)
{
  auto lock = getObjectLock(o);

  auto *obj = (BaseObject *)o;
  auto *array = obj->getParamObject<BaseArray>(name);
  unmapArray((ANARIArray)array);
}

void BaseDevice::commitParameters(ANARIObject o)
{
  if (handleIsDevice(o)) {
    auto lock = scopeLockObject();
    deviceCommitParameters();
  } else {
    auto *obj = (BaseObject *)o;
    m_state->commitBuffer.addObjectToCommit(obj);
    obj->notifyChangeObservers();
  }
}

void BaseDevice::release(ANARIObject o)
{
  if (o == nullptr)
    return;

  if (handleIsDevice(o)) {
    if (--m_refCount == 0)
      delete this;
    return;
  }

  auto &obj = referenceFromHandle(o);

  if (obj.useCount(RefType::PUBLIC) == 0) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "detected too many releases of object (type %s)",
        anari::typenameOf(obj.type()));
    return;
  }

  if (obj.useCount(RefType::PUBLIC) == 1) {
    if (anari::isArray(obj.type()) && obj.useCount(RefType::INTERNAL) > 0)
      ((BaseArray *)o)->privatize();
    else if (obj.type() == ANARI_FRAME) {
      auto *f = (BaseFrame *)o;
      f->discard();
      f->frameReady(ANARI_WAIT);
    }
  }

  obj.refDec(RefType::PUBLIC);
}

void BaseDevice::retain(ANARIObject o)
{
  auto lock = getObjectLock(o);

  if (handleIsDevice(o))
    m_refCount++;
  else
    referenceFromHandle(o).refInc(RefType::PUBLIC);
}

// Frame Manipulation /////////////////////////////////////////////////////////

const void *BaseDevice::frameBufferMap(ANARIFrame f,
    const char *channel,
    uint32_t *w,
    uint32_t *h,
    ANARIDataType *pixelType)
{
  auto lock = getObjectLock(f);
  return referenceFromHandle<BaseFrame>(f).map(channel, w, h, pixelType);
}

void BaseDevice::frameBufferUnmap(ANARIFrame f, const char *channel)
{
  auto lock = getObjectLock(f);
  return referenceFromHandle<BaseFrame>(f).unmap(channel);
}

// Frame Rendering ////////////////////////////////////////////////////////////

void BaseDevice::renderFrame(ANARIFrame f)
{
  auto lock = getObjectLock(f);
  referenceFromHandle<BaseFrame>(f).renderFrame();
}

int BaseDevice::frameReady(ANARIFrame f, ANARIWaitMask m)
{
  auto lock = getObjectLock(f);
  return referenceFromHandle<BaseFrame>(f).frameReady(m);
}

void BaseDevice::discardFrame(ANARIFrame f)
{
  auto lock = getObjectLock(f);
  referenceFromHandle<BaseFrame>(f).discard();
}

// Other BaseDevice definitions ///////////////////////////////////////////////

BaseDevice::BaseDevice(ANARIStatusCallback defaultCallback, const void *userPtr)
{
  anari::DeviceImpl::m_defaultStatusCB = defaultCallback;
  anari::DeviceImpl::m_defaultStatusCBUserPtr = userPtr;
}

BaseDevice::BaseDevice(ANARILibrary l) : DeviceImpl(l) {}

void BaseDevice::deviceCommitParameters()
{
  m_state->statusCB =
      getParam<ANARIStatusCallback>("statusCallback", defaultStatusCallback());
  m_state->statusCBUserPtr = getParam<const void *>(
      "statusCallbackUserData", defaultStatusCallbackUserPtr());
}

BaseDevice::~BaseDevice()
{
  if (!m_state)
    return;

  auto &state = *m_state;

  auto reportLeaks = [&](auto &count, const char *handleType) {
    auto c = count.load();
    if (c != 0) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "detected %zu leaked %s objects",
          c,
          handleType);
    }
  };

  reportLeaks(state.objectCounts.frames, "ANARIFrame");
  reportLeaks(state.objectCounts.cameras, "ANARICamera");
  reportLeaks(state.objectCounts.renderers, "ANARIRenderer");
  reportLeaks(state.objectCounts.worlds, "ANARIWorld");
  reportLeaks(state.objectCounts.instances, "ANARIInstance");
  reportLeaks(state.objectCounts.groups, "ANARIGroup");
  reportLeaks(state.objectCounts.lights, "ANARILight");
  reportLeaks(state.objectCounts.surfaces, "ANARISurface");
  reportLeaks(state.objectCounts.geometries, "ANARIGeometry");
  reportLeaks(state.objectCounts.materials, "ANARIMaterial");
  reportLeaks(state.objectCounts.samplers, "ANARISampler");
  reportLeaks(state.objectCounts.volumes, "ANARIVolume");
  reportLeaks(state.objectCounts.spatialFields, "ANARISpatialField");
  reportLeaks(state.objectCounts.arrays, "ANARIArray");

  if (state.objectCounts.unknown.load() != 0) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "detected %zu leaked ANARIObject objects created of unknown subtype",
        state.objectCounts.unknown.load());
  }
}

int BaseDevice::deviceGetProperty(
    const char *name, ANARIDataType type, void *mem, uint64_t size)
{
  return 0;
}

void BaseDevice::deviceSetParameter(
    const char *id, ANARIDataType type, const void *mem)
{
  setParam(id, type, mem);
}

void BaseDevice::deviceUnsetParameter(const char *id)
{
  removeParam(id);
}

void BaseDevice::deviceUnsetAllParameters()
{
  removeAllParams();
}

std::scoped_lock<std::mutex> BaseDevice::getObjectLock(ANARIObject object)
{
  if (handleIsDevice(object))
    return referenceFromHandle<BaseDevice>(object).scopeLockObject();
  else
    return referenceFromHandle(object).scopeLockObject();
}

} // namespace helium
