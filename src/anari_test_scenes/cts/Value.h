// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ParameterBinding.h"
#include "anari_test_scenes_export.h"
// std
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace anari {
namespace cts {

// An axis value, or any value the harness carries by name. Most values are raw
// ANARI values. Material axes may instead carry a typed ParameterBinding so
// constants, attributes, sampler slots, and unset values stay distinct.
class Any
{
 public:
  Any() = default;
  Any(RawValue value) : m_value(std::move(value)) {}
  Any(ParameterBinding binding) : m_value(std::move(binding)) {}
  Any(ANARIDataType type, const void *value) : m_value(RawValue(type, value)) {}

  template <typename T,
      typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, Any>
          && !std::is_same_v<std::decay_t<T>, RawValue>
          && !std::is_same_v<std::decay_t<T>, ParameterBinding>>>
  Any(T value) : m_value(RawValue(std::move(value)))
  {}

  bool isBinding() const
  {
    return std::holds_alternative<ParameterBinding>(m_value);
  }

  const RawValue &raw() const;
  const ParameterBinding &binding() const;

  bool valid() const
  {
    return isBinding() || raw().valid();
  }

  ANARIDataType type() const
  {
    return raw().type();
  }

  const void *data() const
  {
    return raw().data();
  }

  template <typename T>
  T get() const
  {
    return raw().get<T>();
  }

  std::string getString() const
  {
    return raw().getString();
  }

 private:
  std::variant<RawValue, ParameterBinding> m_value{RawValue{}};
};

// An invalid Any, meaning "leave this parameter unset so the device default is
// used". Mirrors the old JSON `null` permutation value. Serializes to "none".
inline Any none()
{
  return Any{};
}

// Stable, filesystem-safe string for an axis value, used to build Case ids and
// ground-truth keys. Scalars render without trailing zeros, strings pass
// through (sanitized), an invalid value renders as "none".
ANARI_TEST_SCENES_INTERFACE std::string anyToString(const Any &v);

} // namespace cts
} // namespace anari
