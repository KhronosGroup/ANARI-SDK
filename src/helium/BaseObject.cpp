// Copyright 2022-2024 The Khronos Group
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

TimeStamp BaseObject::lastParameterChanged() const
{
  return m_lastParameterChanged;
}

void BaseObject::markParameterChanged()
{
  m_lastParameterChanged = newTimeStamp();
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

TimeStamp BaseObject::lastFinalized() const
{
  return m_lastFinalized;
}

void BaseObject::markFinalized()
{
  m_lastFinalized = newTimeStamp();
}

void BaseObject::addChangeObserver(BaseObject *obj)
{
  m_changeObservers.push_back(obj);
}

void BaseObject::removeChangeObserver(BaseObject *obj)
{
  m_changeObservers.erase(std::remove_if(m_changeObservers.begin(),
                              m_changeObservers.end(),
                              [&](BaseObject *o) -> bool { return o == obj; }),
      m_changeObservers.end());
}

void BaseObject::notifyChangeObservers() const
{
  for (auto o : m_changeObservers)
    notifyChangeObserver(o);
}

BaseGlobalDeviceState *BaseObject::deviceState() const
{
  return m_state;
}

void BaseObject::notifyChangeObserver(BaseObject *o) const
{
  o->markUpdated();
  if (auto *ds = deviceState(); ds)
    ds->commitBuffer.addObjectToFinalize(o);
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
  case ANARI_LIGHT:
    s->objectCounts.lights++;
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
  case ANARI_LIGHT:
    s->objectCounts.lights--;
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
