// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "BaseArray.h"
#include "BaseDevice.h"
#include "BaseFrame.h"
// anari
#include "anari/backend/LibraryImpl.h"
#include "anari/type_utility.h"

namespace helium {

// Data Arrays ////////////////////////////////////////////////////////////////

void *BaseDevice::mapArray(ANARIArray a)
{
  return referenceFromHandle<BaseArray>(a).map();
}

void BaseDevice::unmapArray(ANARIArray a)
{
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
      flushCommitBuffer();
    return referenceFromHandle(object).getProperty(name, type, mem, mask);
  }

  return 0;
}

// Object + Parameter Lifetime Management /////////////////////////////////////

void BaseDevice::setParameter(
    ANARIObject object, const char *name, ANARIDataType type, const void *mem)
{
  if (handleIsDevice(object)) {
    deviceSetParameter(name, type, mem);
    return;
  }
  auto &o = referenceFromHandle(object);
  if (anari::isObject(type) && mem == nullptr)
    o.removeParam(name);
  else
    o.setParam(name, type, mem);
  o.markUpdated();
}

void BaseDevice::unsetParameter(ANARIObject o, const char *name)
{
  if (handleIsDevice(o))
    deviceUnsetParameter(name);
  else {
    auto &obj = referenceFromHandle(o);
    obj.removeParam(name);
    obj.markUpdated();
  }
}


void* BaseDevice::mapParameterArray1D(ANARIObject o,
    const char* name,
    ANARIDataType dataType,
    uint64_t numElements1)
{
  return nullptr;
}

void* BaseDevice::mapParameterArray2D(ANARIObject o,
    const char* name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t numElements2)
{
  return nullptr;
}

void* BaseDevice::mapParameterArray3D(ANARIObject o,
    const char* name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t numElements2,
    uint64_t numElements3)
{
  return nullptr;
}

void BaseDevice::unmapParameterArray(ANARIObject o,
    const char* name)
{

}

void BaseDevice::commitParameters(ANARIObject o)
{
  if (handleIsDevice(o))
    deviceCommitParameters();
  else
    m_state->commitBuffer.addObject((BaseObject *)o);
}

void BaseDevice::release(ANARIObject o)
{
  if (o == nullptr)
    return;
  else if (handleIsDevice(o)) {
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

  bool privatizeArray = anari::isArray(obj.type())
      && obj.useCount(RefType::INTERNAL) > 0
      && obj.useCount(RefType::PUBLIC) == 1;

  obj.refDec(RefType::PUBLIC);

  if (privatizeArray)
    ((BaseArray *)o)->privatize();
}

void BaseDevice::retain(ANARIObject o)
{
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
  return referenceFromHandle<BaseFrame>(f).map(channel, w, h, pixelType);
}

void BaseDevice::frameBufferUnmap(ANARIFrame f, const char *channel)
{
  return referenceFromHandle<BaseFrame>(f).unmap(channel);
}

// Frame Rendering ////////////////////////////////////////////////////////////

void BaseDevice::renderFrame(ANARIFrame f)
{
  referenceFromHandle<BaseFrame>(f).renderFrame();
}

int BaseDevice::frameReady(ANARIFrame f, ANARIWaitMask m)
{
  return referenceFromHandle<BaseFrame>(f).frameReady(m);
}

void BaseDevice::discardFrame(ANARIFrame f)
{
  referenceFromHandle<BaseFrame>(f).discard();
}

// Other BaseDevice definitions ///////////////////////////////////////////////

BaseDevice::BaseDevice(ANARIStatusCallback defaultCallback, const void *userPtr)
{
  anari::DeviceImpl::m_defaultStatusCB = defaultCallback;
  anari::DeviceImpl::m_defaultStatusCBUserPtr = userPtr;
}

BaseDevice::BaseDevice(ANARILibrary l) : DeviceImpl(l) {}

void BaseDevice::flushCommitBuffer()
{
  m_state->commitBuffer.flush();
}

void BaseDevice::clearCommitBuffer()
{
  m_state->commitBuffer.clear();
}

void BaseDevice::deviceCommitParameters()
{
  m_state->statusCB =
      getParam<ANARIStatusCallback>("statusCallback", defaultStatusCallback());
  m_state->statusCBUserPtr = getParam<const void *>(
      "statusCallbackUserData", defaultStatusCallbackUserPtr());
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

} // namespace helium
