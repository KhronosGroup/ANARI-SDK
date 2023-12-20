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
{
  incrementObjectCount();
}

BaseObject::~BaseObject()
{
  decrementObjectCount();
}

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

void BaseObject::addCommitObserver(BaseObject *obj)
{
  m_observers.push_back(obj);
}

void BaseObject::removeCommitObserver(BaseObject *obj)
{
  m_observers.erase(std::remove_if(m_observers.begin(),
                        m_observers.end(),
                        [&](BaseObject *o) -> bool { return o == obj; }),
      m_observers.end());
}

void BaseObject::notifyCommitObservers() const
{
  for (auto o : m_observers)
    notifyObserver(o);
}

BaseGlobalDeviceState *BaseObject::deviceState() const
{
  return m_state;
}

void BaseObject::notifyObserver(BaseObject *obj) const
{
  // no-op
}

void BaseObject::incrementObjectCount()
{
  auto *s = deviceState();
  if (!s)
    return;

  switch (type()) {
  case ANARI_FRAME:
    s->objectCounts.frames++;
    break;
  case ANARI_CAMERA:
    s->objectCounts.cameras++;
    break;
  case ANARI_RENDERER:
    s->objectCounts.renderers++;
    break;
  case ANARI_WORLD:
    s->objectCounts.worlds++;
    break;
  case ANARI_INSTANCE:
    s->objectCounts.instances++;
    break;
  case ANARI_GROUP:
    s->objectCounts.groups++;
    break;
  case ANARI_SURFACE:
    s->objectCounts.surfaces++;
    break;
  case ANARI_GEOMETRY:
    s->objectCounts.geometries++;
    break;
  case ANARI_MATERIAL:
    s->objectCounts.materials++;
    break;
  case ANARI_SAMPLER:
    s->objectCounts.samplers++;
    break;
  case ANARI_VOLUME:
    s->objectCounts.volumes++;
    break;
  case ANARI_SPATIAL_FIELD:
    s->objectCounts.spatialFields++;
    break;
  case ANARI_ARRAY:
  case ANARI_ARRAY1D:
  case ANARI_ARRAY2D:
  case ANARI_ARRAY3D:
    s->objectCounts.arrays++;
    break;
  case ANARI_UNKNOWN:
  default:
    s->objectCounts.unknown++;
    break;
  }
}

void BaseObject::decrementObjectCount()
{
  auto *s = deviceState();
  if (!s)
    return;

  switch (type()) {
  case ANARI_FRAME:
    s->objectCounts.frames--;
    break;
  case ANARI_CAMERA:
    s->objectCounts.cameras--;
    break;
  case ANARI_RENDERER:
    s->objectCounts.renderers--;
    break;
  case ANARI_WORLD:
    s->objectCounts.worlds--;
    break;
  case ANARI_INSTANCE:
    s->objectCounts.instances--;
    break;
  case ANARI_GROUP:
    s->objectCounts.groups--;
    break;
  case ANARI_SURFACE:
    s->objectCounts.surfaces--;
    break;
  case ANARI_GEOMETRY:
    s->objectCounts.geometries--;
    break;
  case ANARI_MATERIAL:
    s->objectCounts.materials--;
    break;
  case ANARI_SAMPLER:
    s->objectCounts.samplers--;
    break;
  case ANARI_VOLUME:
    s->objectCounts.volumes--;
    break;
  case ANARI_SPATIAL_FIELD:
    s->objectCounts.spatialFields--;
    break;
  case ANARI_ARRAY:
  case ANARI_ARRAY1D:
  case ANARI_ARRAY2D:
  case ANARI_ARRAY3D:
    s->objectCounts.arrays--;
    break;
  case ANARI_UNKNOWN:
  default:
    s->objectCounts.unknown--;
    break;
  }
}

} // namespace helium

HELIUM_ANARI_TYPEFOR_DEFINITION(helium::BaseObject *);
