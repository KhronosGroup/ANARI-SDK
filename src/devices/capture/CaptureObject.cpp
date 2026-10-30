// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "CaptureObject.h"

namespace capture_device {

// CommittedParams ////////////////////////////////////////////////////////////

const CommittedParams::List &CommittedParams::committedParams() const
{
  return m_committed;
}

const std::string &CommittedParams::subtype() const
{
  return m_subtype;
}

void CommittedParams::readCommitted(const helium::ParameterizedObject &obj)
{
  m_committed.clear();
  for (const auto &name : obj.paramNames())
    m_committed.emplace_back(name, obj.getParamDirect(name));
}

// Object /////////////////////////////////////////////////////////////////////

Object::Object(ANARIDataType type, const char *subtype, CaptureState *state)
    : helium::BaseObject(type, state)
{
  m_subtype = subtype ? subtype : "";
}

bool Object::isValid() const
{
  return true;
}

bool Object::getProperty(
    const std::string_view &, ANARIDataType, void *, uint64_t, uint32_t)
{
  return false;
}

void Object::commitParameters()
{
  readCommitted(*this);
}

void Object::finalize() {}

} // namespace capture_device
