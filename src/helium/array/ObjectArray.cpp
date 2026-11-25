// Copyright 2023-2026 The Khronos Group
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
  syncAppHandles();
  updateLiveHandles();
}

ObjectArray::~ObjectArray()
{
  std::for_each(m_appHandles.begin(), m_appHandles.end(), refDecObject);
  std::for_each(
      m_appendedHandles.begin(), m_appendedHandles.end(), refDecObject);
}

void ObjectArray::commitParameters()
{
  m_begin = size_t(getParam<uint64_t>("begin", 0));
  m_begin = std::clamp(m_begin, size_t(0), m_capacity - 1);
  m_end = size_t(getParam<uint64_t>("end", m_capacity));
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
  updateLiveHandles();
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

void ObjectArray::unmap()
{
  if (isMapped()) {
    syncAppHandles();
    updateLiveHandles();
  }
  Array::unmap();
}

BaseObject **ObjectArray::handlesBegin() const
{
  return m_liveHandles.data();
}

BaseObject **ObjectArray::handlesEnd() const
{
  return handlesBegin() + totalSize();
}

void ObjectArray::appendHandle(BaseObject *o)
{
  o->refInc(helium::RefType::INTERNAL);
  m_appendedHandles.push_back(o);
  updateLiveHandles();
}

void ObjectArray::removeAppendedHandles()
{
  m_liveHandles.resize(size());
  for (auto o : m_appendedHandles)
    o->refDec(helium::RefType::INTERNAL);
  m_appendedHandles.clear();
}

void ObjectArray::syncAppHandles()
{
  // After privatization the app's memory is gone; m_appHandles keeps the
  // handles it last held.
  if (!data())
    return;

  auto **srcBegin = (BaseObject **)data();
  auto **srcEnd = srcBegin + totalCapacity();
  std::for_each(srcBegin, srcEnd, refIncObject);
  std::for_each(m_appHandles.begin(), m_appHandles.end(), refDecObject);
  std::copy(srcBegin, srcEnd, m_appHandles.data());
}

void ObjectArray::updateLiveHandles()
{
  m_liveHandles.resize(totalSize());
  auto liveEnd = std::copy(m_appHandles.begin() + m_begin,
      m_appHandles.begin() + m_end,
      m_liveHandles.begin());
  std::copy(m_appendedHandles.begin(), m_appendedHandles.end(), liveEnd);
}

bool ObjectArray::privatizeCopiesAppData() const
{
  return false;
}

void ObjectArray::privatize()
{
  // Copies nothing for an object array (m_appHandles already holds references
  // to all totalCapacity() handles); it only marks the array privatized.
  makePrivatizedCopy(totalCapacity());
  freeAppMemory();
  if (data()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "ObjectArray privatized but host array still present");
  }
}

} // namespace helium

HELIUM_ANARI_TYPEFOR_DEFINITION(helium::ObjectArray *);
