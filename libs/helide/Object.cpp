// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Object.h"
// std
#include <atomic>
#include <cstdarg>

namespace helide {

// Object definitions /////////////////////////////////////////////////////////

Object::Object(ANARIDataType type, HelideGlobalState *s)
    : helium::BaseObject(type, s)
{}

void Object::commit()
{
  // no-op
}

bool Object::getProperty(
    const std::string_view &name, ANARIDataType type, void *ptr, uint32_t flags)
{
  if (name == "valid" && type == ANARI_BOOL) {
    helium::writeToVoidP(ptr, isValid());
    return true;
  }

  return false;
}

bool Object::isValid() const
{
  return true;
}

HelideGlobalState *Object::deviceState() const
{
  return (HelideGlobalState *)helium::BaseObject::m_state;
}

// UnknownObject definitions //////////////////////////////////////////////////

static size_t s_numUnknownObjects = 0;

size_t UnknownObject::objectCount()
{
  return s_numUnknownObjects;
}

UnknownObject::UnknownObject(ANARIDataType type, HelideGlobalState *s)
    : Object(type, s)
{
  s_numUnknownObjects++;
}

UnknownObject::~UnknownObject()
{
  s_numUnknownObjects--;
}

bool UnknownObject::isValid() const
{
  return false;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Object *);
