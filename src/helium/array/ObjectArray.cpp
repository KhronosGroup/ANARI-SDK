// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "array/ObjectArray.h"

namespace helium {

// Helper functions ///////////////////////////////////////////////////////////

static void refIncObject(BaseObject *obj)
{
  if (obj)
    obj->refInc(helium::RefType::INTERNAL);
}

static void refDecObject(BaseObject *obj)
{
  if (obj)
    obj->refDec(helium::RefType::INTERNAL);
}

// ObjectArray definitions ////////////////////////////////////////////////////

ObjectArray::ObjectArray(
    BaseGlobalDeviceState *state, const Array1DMemoryDescriptor &d)
    : Array(ANARI_ARRAY1D, state, d), m_capacity(d.numItems), m_end(d.numItems)
{
  m_appHandles.resize(d.numItems, nullptr);
  initManagedMemory();
  updateInternalHandleArrays();
}

ObjectArray::~ObjectArray()
{
  std::for_each(m_appHandles.begin(), m_appHandles.end(), refDecObject);
  std::for_each(
      m_appendedHandles.begin(), m_appendedHandles.end(), refDecObject);
}

void ObjectArray::commitParameters()
{
  m_begin = getParam<size_t>("begin", 0);
  m_begin = std::clamp(m_begin, size_t(0), m_capacity - 1);
  m_end = getParam<size_t>("end", m_capacity);
  m_end = std::clamp(m_end, size_t(1), m_capacity);

  if (size() == 0) {
    reportMessage(ANARI_SEVERITY_ERROR, "array size must be greater than zero");
    return;
  }

  if (m_begin > m_end) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "array 'begin' is not less than 'end', swapping values");
    std::swap(m_begin, m_end);
  }
}

void ObjectArray::finalize()
{
  markDataModified();
  notifyChangeObservers();
}

size_t ObjectArray::totalSize() const
{
  return size() + m_appendedHandles.size();
}

size_t ObjectArray::totalCapacity() const
{
  return m_capacity;
}

size_t ObjectArray::size() const
{
  return m_end - m_begin;
}

void ObjectArray::privatize()
{
  makePrivatizedCopy(size());
  freeAppMemory();
  if (data()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "ObjectArray privatized but host array still present");
  }
}

void ObjectArray::unmap()
{
  if (isMapped())
    updateInternalHandleArrays();
  Array::unmap();
}

BaseObject **ObjectArray::handlesBegin() const
{
  return m_liveHandles.data() + m_begin;
}

BaseObject **ObjectArray::handlesEnd() const
{
  return handlesBegin() + totalSize();
}

void ObjectArray::appendHandle(BaseObject *o)
{
  o->refInc(helium::RefType::INTERNAL);
  m_appendedHandles.push_back(o);
  updateInternalHandleArrays();
}

void ObjectArray::removeAppendedHandles()
{
  m_liveHandles.resize(size());
  for (auto o : m_appendedHandles)
    o->refDec(helium::RefType::INTERNAL);
  m_appendedHandles.clear();
}

void ObjectArray::updateInternalHandleArrays() const
{
  m_liveHandles.resize(totalSize());

  if (data()) {
    auto **srcAllBegin = (BaseObject **)data();
    auto **srcAllEnd = srcAllBegin + totalCapacity();
    std::for_each(srcAllBegin, srcAllEnd, refIncObject);
    std::for_each(m_appHandles.begin(), m_appHandles.end(), refDecObject);
    std::copy(srcAllBegin, srcAllEnd, m_appHandles.data());

    auto **srcRegionBegin = srcAllBegin + m_begin;
    auto **srcRegionEnd = srcRegionBegin + size();
    std::copy(srcRegionBegin, srcRegionEnd, m_liveHandles.data());
  }

  std::copy(m_appendedHandles.begin(),
      m_appendedHandles.end(),
      m_liveHandles.begin() + size());
}

} // namespace helium

HELIUM_ANARI_TYPEFOR_DEFINITION(helium::ObjectArray *);
