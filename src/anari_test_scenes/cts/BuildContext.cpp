// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "BuildContext.h"

namespace anari {
namespace cts {

BuildContext::BuildContext(anari::Device device) : m_device(device) {}

anari::Device BuildContext::device() const
{
  return m_device;
}

std::string BuildContext::getString(
    const std::string &name, const std::string &valIfNotFound) const
{
  return m_params.getParamString(name, valIfNotFound);
}

bool BuildContext::has(const std::string &name) const
{
  return m_params.hasParam(name);
}

void BuildContext::set(const std::string &name, const Any &value)
{
  // A `none()` value means "leave unset"; dropping it lets get<>() fall back to
  // the default the build() function supplies.
  if (value.valid())
    m_params.setParamDirect(name, value);
}

} // namespace cts
} // namespace anari
