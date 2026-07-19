// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Object.h"

namespace helide_gpu {

// Object definitions /////////////////////////////////////////////////////////

Object::Object(ANARIDataType type, HelideGPUDeviceGlobalState *s)
    : helium::BaseObject(type, s)
{}

void Object::commitParameters()
{
  // no-op
}

void Object::finalize()
{
  // no-op
}

bool Object::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t size,
    uint32_t flags)
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

HelideGPUDeviceGlobalState *Object::deviceState() const
{
  return (HelideGPUDeviceGlobalState *)helium::BaseObject::m_state;
}

// UnknownObject definitions //////////////////////////////////////////////////

UnknownObject::UnknownObject(ANARIDataType type, HelideGPUDeviceGlobalState *s)
    : Object(type, s)
{}

bool UnknownObject::isValid() const
{
  return false;
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_DEFINITION(helide_gpu::Object *);
