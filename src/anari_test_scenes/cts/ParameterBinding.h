// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include "anari/anari_cpp.hpp"
// helium
#include "helium/utility/AnariAny.h"
// std
#include <cstddef>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace anari {
namespace cts {

using RawValue = helium::AnariAny;

struct UnsetParameter
{
};

struct ConstantParameter
{
  RawValue value;
};

struct AttributeParameter
{
  std::string name;
};

struct SamplerParameter
{
  size_t index{0};
};

// A material parameter's source. Callers cannot accidentally confuse an
// attribute name with a literal string, or a sampler slot with either one.
struct ParameterBinding
{
  using Value = std::variant<UnsetParameter,
      ConstantParameter,
      AttributeParameter,
      SamplerParameter>;

  static ParameterBinding unset();
  static ParameterBinding constant(RawValue value);
  static ParameterBinding attribute(std::string name);
  static ParameterBinding sampler(size_t index);

  const Value &value() const;

 private:
  explicit ParameterBinding(Value value);

  Value m_value;
};

inline ParameterBinding unsetBinding()
{
  return ParameterBinding::unset();
}

template <typename T>
inline ParameterBinding constantBinding(T value)
{
  return ParameterBinding::constant(RawValue(std::move(value)));
}

inline ParameterBinding attributeBinding(std::string name)
{
  return ParameterBinding::attribute(std::move(name));
}

inline ParameterBinding samplerBinding(size_t index)
{
  return ParameterBinding::sampler(index);
}

namespace detail {

void applyParameterValueImpl(anari::Device d,
    anari::Object object,
    const char *parameter,
    const RawValue &value);

void applyParameterBindingImpl(anari::Device d,
    anari::Object object,
    const char *parameter,
    const ParameterBinding &binding,
    const std::vector<anari::Sampler> &samplers);

} // namespace detail

// Apply a regular axis value. An empty RawValue leaves the parameter unset;
// object handles and unsupported value types are rejected before the ANARI
// call.
template <typename H>
inline void applyParameterValue(
    anari::Device d, H object, const char *parameter, const RawValue &value)
{
  detail::applyParameterValueImpl(
      d, reinterpret_cast<anari::Object>(object), parameter, value);
}

// Resolve and apply a typed material binding. Sampler indices are checked
// before the ANARI call so malformed Test definitions fail deterministically.
template <typename H>
inline void applyParameterBinding(anari::Device d,
    H object,
    const char *parameter,
    const ParameterBinding &binding,
    const std::vector<anari::Sampler> &samplers = {})
{
  detail::applyParameterBindingImpl(
      d, reinterpret_cast<anari::Object>(object), parameter, binding, samplers);
}

} // namespace cts
} // namespace anari
