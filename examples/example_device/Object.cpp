// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Object.h"

namespace anari {
namespace example_device {

std::atomic<TimeStamp> g_timeStamp = []() { return 0; }();

TimeStamp newTimeStamp()
{
  return g_timeStamp++;
}

box3 Object::bounds() const
{
  return box3();
}

void Object::commit()
{
  // no-op
}

bool Object::getProperty(
    const std::string &, ANARIDataType, void *, ANARIWaitMask)
{
  return false;
}

void Object::setObjectType(ANARIDataType type)
{
  m_type = type;
}

ANARIDataType Object::type() const
{
  return m_type;
}

TimeStamp Object::lastUpdated() const
{
  return m_lastUpdated;
}

void Object::markUpdated()
{
  m_lastUpdated = newTimeStamp();
}

int Object::commitPriority() const
{
  return m_commitPriority;
}

void Object::setCommitPriority(int p)
{
  m_commitPriority = p;
}

} // namespace example_device

ANARI_TYPEFOR_DEFINITION(example_device::Object *);

} // namespace anari
