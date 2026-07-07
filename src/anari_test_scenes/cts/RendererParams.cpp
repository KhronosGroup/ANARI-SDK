// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "RendererParams.h"
// std
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace anari {
namespace cts {

namespace {

std::string trim(const std::string &s)
{
  const auto first = s.find_first_not_of(" \t");
  if (first == std::string::npos)
    return "";
  const auto last = s.find_last_not_of(" \t");
  return s.substr(first, last - first + 1);
}

// Parse the whole string as a float; false on trailing garbage or non-finite.
bool parseFloat(const std::string &s, float &out)
{
  const std::string t = trim(s);
  if (t.empty())
    return false;
  try {
    size_t pos = 0;
    out = std::stof(t, &pos);
    return pos == t.size();
  } catch (const std::exception &) {
    return false;
  }
}

// Parse the whole string as a base-10 integer; false on trailing garbage.
bool parseInt(const std::string &s, long &out)
{
  const std::string t = trim(s);
  if (t.empty())
    return false;
  try {
    size_t pos = 0;
    out = std::stol(t, &pos, 10);
    return pos == t.size();
  } catch (const std::exception &) {
    return false;
  }
}

std::vector<std::string> splitCommas(const std::string &s)
{
  std::vector<std::string> parts;
  std::stringstream ss(s);
  std::string part;
  while (std::getline(ss, part, ','))
    parts.push_back(trim(part));
  return parts;
}

template <typename T>
void appendBytes(std::vector<std::byte> &out, T v)
{
  const auto *p = reinterpret_cast<const std::byte *>(&v);
  out.insert(out.end(), p, p + sizeof(T));
}

ANARIDataType floatVectorType(size_t components)
{
  switch (components) {
  case 2:
    return ANARI_FLOAT32_VEC2;
  case 3:
    return ANARI_FLOAT32_VEC3;
  case 4:
    return ANARI_FLOAT32_VEC4;
  default:
    return ANARI_UNKNOWN;
  }
}

std::optional<ParsedParam> parseFloatVector(
    const std::string &text, ANARIDataType type, size_t components)
{
  const auto parts = splitCommas(text);
  if (parts.size() != components)
    return {};
  ParsedParam out;
  out.type = type;
  for (const auto &p : parts) {
    float v = 0.f;
    if (!parseFloat(p, v))
      return {};
    appendBytes(out.bytes, v);
  }
  return out;
}

} // namespace

std::optional<RendererParam> parseRendererParam(const std::string &arg)
{
  const auto eq = arg.find('=');
  if (eq == std::string::npos)
    return {};
  RendererParam p;
  p.name = trim(arg.substr(0, eq));
  p.value = arg.substr(eq + 1);
  if (p.name.empty())
    return {};
  return p;
}

ANARIDataType inferParamType(const std::string &text)
{
  const std::string t = trim(text);
  if (t == "true" || t == "false")
    return ANARI_BOOL;

  if (t.find(',') != std::string::npos) {
    const auto parts = splitCommas(t);
    if (floatVectorType(parts.size()) != ANARI_UNKNOWN) {
      bool allFloats = true;
      for (const auto &p : parts) {
        float v = 0.f;
        allFloats = allFloats && parseFloat(p, v);
      }
      if (allFloats)
        return floatVectorType(parts.size());
    }
    return ANARI_STRING;
  }

  long i = 0;
  if (parseInt(t, i))
    return ANARI_INT32;
  float f = 0.f;
  if (parseFloat(t, f))
    return ANARI_FLOAT32;
  return ANARI_STRING;
}

std::optional<ParsedParam> parseParamValue(
    ANARIDataType type, const std::string &text)
{
  const ANARIDataType resolved =
      type == ANARI_UNKNOWN ? inferParamType(text) : type;

  ParsedParam out;
  out.type = resolved;
  switch (resolved) {
  case ANARI_BOOL: {
    const std::string t = trim(text);
    if (t == "true" || t == "1")
      appendBytes<int8_t>(out.bytes, 1);
    else if (t == "false" || t == "0")
      appendBytes<int8_t>(out.bytes, 0);
    else
      return {};
    return out;
  }
  case ANARI_INT32: {
    long v = 0;
    if (!parseInt(text, v))
      return {};
    appendBytes<int32_t>(out.bytes, static_cast<int32_t>(v));
    return out;
  }
  case ANARI_UINT32: {
    long v = 0;
    if (!parseInt(text, v) || v < 0)
      return {};
    appendBytes<uint32_t>(out.bytes, static_cast<uint32_t>(v));
    return out;
  }
  case ANARI_FLOAT32: {
    float v = 0.f;
    if (!parseFloat(text, v))
      return {};
    appendBytes<float>(out.bytes, v);
    return out;
  }
  case ANARI_FLOAT32_VEC2:
    return parseFloatVector(text, resolved, 2);
  case ANARI_FLOAT32_VEC3:
    return parseFloatVector(text, resolved, 3);
  case ANARI_FLOAT32_VEC4:
    return parseFloatVector(text, resolved, 4);
  case ANARI_STRING: {
    out.bytes.assign(reinterpret_cast<const std::byte *>(text.c_str()),
        reinterpret_cast<const std::byte *>(text.c_str()) + text.size() + 1);
    return out;
  }
  default:
    return {};
  }
}

} // namespace cts
} // namespace anari
