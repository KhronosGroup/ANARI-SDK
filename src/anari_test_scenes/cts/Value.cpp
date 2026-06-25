// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Value.h"
// std
#include <cctype>
#include <cstdint>

namespace anari {
namespace cts {

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

std::string anyToString(const Any &v)
{
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
    return "none";
  }
}

} // namespace cts
} // namespace anari
