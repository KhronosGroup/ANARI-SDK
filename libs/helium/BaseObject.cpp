// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "BaseObject.h"
// std
#include <cstdarg>

namespace helium {

// Helper functions ///////////////////////////////////////////////////////////

int commitPriority(ANARIDataType type)
{
  switch (type) {
  case ANARI_FRAME:
    return 6;
  case ANARI_WORLD:
    return 5;
  case ANARI_INSTANCE:
    return 4;
  case ANARI_GROUP:
    return 3;
  case ANARI_SURFACE:
  case ANARI_VOLUME:
    return 2;
  case ANARI_MATERIAL:
    return 1;
  default:
    return 0;
  }
}

std::string string_printf(const char *fmt, ...)
{
  std::string s;
  va_list args, args2;
  va_start(args, fmt);
  va_copy(args2, args);

  s.resize(vsnprintf(nullptr, 0, fmt, args2) + 1);
  va_end(args2);
  vsprintf(s.data(), fmt, args);
  va_end(args);
  s.pop_back();
  return s;
}

// BaseObject definitions /////////////////////////////////////////////////////

BaseObject::BaseObject(ANARIDataType type, BaseGlobalDeviceState *state)
    : m_type(type), m_state(state)
{}

bool BaseObject::isValid() const
{
  return true;
}

ANARIDataType BaseObject::type() const
{
  return m_type;
}

TimeStamp BaseObject::lastUpdated() const
{
  return m_lastUpdated;
}

void BaseObject::markUpdated()
{
  m_lastUpdated = newTimeStamp();
}

TimeStamp BaseObject::lastCommitted() const
{
  return m_lastCommitted;
}

void BaseObject::markCommitted()
{
  m_lastCommitted = newTimeStamp();
}

} // namespace helium

HELIUM_ANARI_TYPEFOR_DEFINITION(helium::BaseObject *);
