// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Object.h"

namespace anari_hydra {

Object::Object(ANARIDataType type, HydraGlobalState *s)
    : helium::BaseObject(type, s)
{
  markUpdated();
}

bool Object::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t size,
    uint32_t flags)
{
  return false;
}

void Object::commitParameters()
{
  // Base objects have no parameters to interpret.
}

void Object::finalize()
{
  // Base objects contribute nothing to the mirror.
}

bool Object::isValid() const
{
  return true;
}

HydraGlobalState *Object::deviceState() const
{
  return asHydraState(m_state);
}

// UnknownObject //////////////////////////////////////////////////////////////

UnknownObject::UnknownObject(ANARIDataType type, HydraGlobalState *s)
    : Object(type, s)
{}

bool UnknownObject::isValid() const
{
  return false;
}

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_DEFINITION(anari_hydra::Object *);
}
