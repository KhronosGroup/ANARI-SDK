// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Value.h"
// std
#include <cctype>
#include <cstdint>
#include <stdexcept>

namespace anari {
namespace cts {

const RawValue &Any::raw() const
{
  if (isBinding())
    throw std::logic_error("typed parameter binding is not a raw ANARI value");
  return std::get<RawValue>(m_value);
}

const ParameterBinding &Any::binding() const
{
  if (!isBinding())
    throw std::logic_error("raw ANARI value is not a parameter binding");
  return std::get<ParameterBinding>(m_value);
}

// Render a floating point value without a fixed number of trailing zeros so
// keys stay short and readable (100.0 -> "100", 0.6 -> "0.6").
static std::string trimFloat(double d)
{
  std::string s = std::to_string(d);
  if (s.find('.') != std::string::npos) {
    s.erase(s.find_last_not_of('0') + 1);
    if (!s.empty() && s.back() == '.')
      s.pop_back();
  }
  return s;
}

// Keep keys safe to use as path segments across platforms.
static std::string sanitize(const std::string &in)
{
  std::string out;
  out.reserve(in.size());
  for (char c : in) {
    const bool ok = std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.'
        || c == '_' || c == '+' || c == '-';
    out.push_back(ok ? c : '-');
  }
  return out;
}

// Render a fixed-length, float-component value (vec2/3/4, mat*, box*) as its
// components joined by commas. Distinct vector values must yield distinct keys
// so they don't collide a shared ground-truth path.
static std::string floatComponents(const Any &v)
{
  const size_t n = anari::sizeOf(v.type()) / sizeof(float);
  const auto *f = static_cast<const float *>(v.data());
  std::string out;
  for (size_t i = 0; i < n; ++i) {
    if (i)
      out.push_back(',');
    out += trimFloat(f[i]);
  }
  return out;
}

// True for the fixed-length float-composite ANARI types the harness uses as
// axis values (vectors, matrices, boxes). These are read component-wise.
static bool isFloatComposite(ANARIDataType t)
{
  switch (t) {
  case ANARI_FLOAT32_VEC2:
  case ANARI_FLOAT32_VEC3:
  case ANARI_FLOAT32_VEC4:
  case ANARI_FLOAT32_MAT2:
  case ANARI_FLOAT32_MAT3:
  case ANARI_FLOAT32_MAT4:
  case ANARI_FLOAT32_BOX1:
  case ANARI_FLOAT32_BOX2:
  case ANARI_FLOAT32_BOX3:
    return true;
  default:
    return false;
  }
}

std::string anyToString(const Any &v)
{
  if (v.isBinding()) {
    return std::visit(
        [](const auto &source) -> std::string {
          using T = std::decay_t<decltype(source)>;
          if constexpr (std::is_same_v<T, UnsetParameter>) {
            return "none";
          } else if constexpr (std::is_same_v<T, ConstantParameter>) {
            return anyToString(Any(source.value));
          } else if constexpr (std::is_same_v<T, AttributeParameter>) {
            return sanitize(source.name);
          } else {
            // Compatibility label only: the sampler reference remains typed in
            // memory, while catalog metadata and ground-truth keys stay stable.
            return "ref_sampler_" + std::to_string(source.index);
          }
        },
        v.binding().value());
  }

  if (!v.valid())
    return "none";

  switch (v.type()) {
  case ANARI_BOOL:
    return v.get<bool>() ? "true" : "false";
  case ANARI_INT32:
    return std::to_string(v.get<int32_t>());
  case ANARI_UINT32:
    return std::to_string(v.get<uint32_t>());
  case ANARI_INT64:
    return std::to_string(v.get<int64_t>());
  case ANARI_UINT64:
    return std::to_string(v.get<uint64_t>());
  case ANARI_FLOAT32:
    return sanitize(trimFloat(v.get<float>()));
  case ANARI_FLOAT64:
    return sanitize(trimFloat(v.get<double>()));
  case ANARI_STRING:
    return sanitize(v.getString());
  default:
    if (isFloatComposite(v.type()))
      return sanitize(floatComponents(v));
    return "none";
  }
}

} // namespace cts
} // namespace anari
