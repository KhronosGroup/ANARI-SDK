// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "BuildContext.h"
// std
#include <stdexcept>

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
  return m_params.hasParam(name) || m_bindings.find(name) != m_bindings.end();
}

Any BuildContext::value(const std::string &name) const
{
  const auto binding = m_bindings.find(name);
  if (binding != m_bindings.end())
    return Any(binding->second);
  return m_params.getParamDirect(name);
}

const ParameterBinding &BuildContext::binding(const std::string &name) const
{
  const auto it = m_bindings.find(name);
  if (it == m_bindings.end()) {
    throw std::invalid_argument(
        "axis '" + name + "' does not contain a typed parameter binding");
  }
  return it->second;
}

void BuildContext::set(const std::string &name, const Any &value)
{
  if (value.isBinding()) {
    m_bindings.insert_or_assign(name, value.binding());
    return;
  }

  m_bindings.erase(name);
  // A `none()` value means "leave unset"; dropping it lets get<>() fall back to
  // the default the build() function supplies.
  if (value.valid())
    m_params.setParamDirect(name, value.raw());
}

} // namespace cts
} // namespace anari
