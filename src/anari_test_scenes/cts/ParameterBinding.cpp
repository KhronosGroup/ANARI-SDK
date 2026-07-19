// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ParameterBinding.h"
// std
#include <stdexcept>
#include <type_traits>

namespace anari {
namespace cts {

namespace {

std::string parameterName(const char *parameter)
{
  return parameter && *parameter ? parameter : "<unnamed>";
}

void validateConstant(const RawValue &value)
{
  if (!value.valid()) {
    throw std::invalid_argument(
        "constant parameter binding requires a valid ANARI value");
  }
  if (anari::isObject(value.type())) {
    throw std::invalid_argument(
        "constant parameter bindings cannot carry ANARI object handles");
  }
  if (value.type() == ANARI_VOID_POINTER || value.type() == ANARI_STRING_LIST) {
    throw std::invalid_argument(
        std::string("unsupported constant parameter type ")
        + anari::toString(value.type()));
  }
}

void setRawValue(anari::Device d,
    anari::Object object,
    const char *parameter,
    const RawValue &value)
{
  validateConstant(value);
  if (value.type() == ANARI_STRING) {
    const std::string stringValue = value.getString();
    anariSetParameter(d, object, parameter, ANARI_STRING, stringValue.c_str());
  } else {
    anariSetParameter(d, object, parameter, value.type(), value.data());
  }
}

} // namespace

ParameterBinding::ParameterBinding(Value value) : m_value(std::move(value)) {}

ParameterBinding ParameterBinding::unset()
{
  return ParameterBinding(UnsetParameter{});
}

ParameterBinding ParameterBinding::constant(RawValue value)
{
  validateConstant(value);
  return ParameterBinding(ConstantParameter{std::move(value)});
}

ParameterBinding ParameterBinding::attribute(std::string name)
{
  if (name.empty())
    throw std::invalid_argument("parameter binding attribute name is empty");
  return ParameterBinding(AttributeParameter{std::move(name)});
}

ParameterBinding ParameterBinding::sampler(size_t index)
{
  return ParameterBinding(SamplerParameter{index});
}

const ParameterBinding::Value &ParameterBinding::value() const
{
  return m_value;
}

namespace detail {

void applyParameterValueImpl(anari::Device d,
    anari::Object object,
    const char *parameter,
    const RawValue &value)
{
  if (!value.valid())
    return;

  try {
    setRawValue(d, object, parameter, value);
  } catch (const std::invalid_argument &e) {
    throw std::invalid_argument(
        "parameter '" + parameterName(parameter) + "': " + e.what());
  }
}

void applyParameterBindingImpl(anari::Device d,
    anari::Object object,
    const char *parameter,
    const ParameterBinding &binding,
    const std::vector<anari::Sampler> &samplers)
{
  std::visit(
      [&](const auto &source) {
        using T = std::decay_t<decltype(source)>;
        if constexpr (std::is_same_v<T, UnsetParameter>) {
          return;
        } else if constexpr (std::is_same_v<T, ConstantParameter>) {
          setRawValue(d, object, parameter, source.value);
        } else if constexpr (std::is_same_v<T, AttributeParameter>) {
          anariSetParameter(
              d, object, parameter, ANARI_STRING, source.name.c_str());
        } else {
          if (source.index >= samplers.size()) {
            throw std::out_of_range("parameter '" + parameterName(parameter)
                + "' references sampler index " + std::to_string(source.index)
                + ", but only " + std::to_string(samplers.size())
                + " sampler(s) exist");
          }
          anari::Sampler sampler = samplers[source.index];
          anariSetParameter(d, object, parameter, ANARI_SAMPLER, &sampler);
        }
      },
      binding.value());
}

} // namespace detail

} // namespace cts
} // namespace anari
