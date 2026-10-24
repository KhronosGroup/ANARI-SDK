// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "CaptureFormat.h"
// std
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace capture_device {

namespace {

bool startsWith(const std::string &s, const char *prefix)
{
  return s.rfind(prefix, 0) == 0;
}

float halfToFloat(uint16_t h)
{
  const uint32_t sign = (h >> 15) & 1u;
  const uint32_t exponent = (h >> 10) & 0x1fu;
  const uint32_t mantissa = h & 0x3ffu;
  float value;
  if (exponent == 0)
    value = std::ldexp(float(mantissa), -24);
  else if (exponent == 31)
    value = mantissa ? NAN : INFINITY;
  else
    value = std::ldexp(float(mantissa | 0x400u), int(exponent) - 25);
  return sign ? -value : value;
}

} // namespace

ElementLayout elementLayout(ANARIDataType type)
{
  ElementLayout layout;
  const std::string name = anari::toString(type);
  if (type == ANARI_BOOL) {
    layout.kind = ScalarKind::BOOL;
  } else if (startsWith(name, "ANARI_FLOAT")) {
    layout.kind = ScalarKind::FLOAT;
  } else if (startsWith(name, "ANARI_UFIXED")
      || startsWith(name, "ANARI_UINT")) {
    layout.kind = ScalarKind::UINT;
  } else if (startsWith(name, "ANARI_FIXED") || startsWith(name, "ANARI_INT")) {
    layout.kind = ScalarKind::INT;
  } else {
    return layout;
  }
  layout.components = anari::componentsOf(type);
  if (layout.components == 0)
    return ElementLayout{};
  layout.componentBytes = anari::sizeOf(type) / layout.components;
  return layout;
}

Scalar readScalar(const ElementLayout &layout, const uint8_t *element, size_t c)
{
  Scalar s;
  s.kind = layout.kind;
  s.bytes = layout.componentBytes;
  const uint8_t *p = element + c * layout.componentBytes;
  switch (layout.kind) {
  case ScalarKind::FLOAT:
    if (s.bytes == 2) {
      uint16_t h;
      std::memcpy(&h, p, 2);
      s.f = halfToFloat(h);
    } else if (s.bytes == 4) {
      float v;
      std::memcpy(&v, p, 4);
      s.f = v;
    } else {
      std::memcpy(&s.f, p, 8);
    }
    break;
  case ScalarKind::INT:
    if (s.bytes == 1) {
      int8_t v;
      std::memcpy(&v, p, 1);
      s.i = v;
    } else if (s.bytes == 2) {
      int16_t v;
      std::memcpy(&v, p, 2);
      s.i = v;
    } else if (s.bytes == 4) {
      int32_t v;
      std::memcpy(&v, p, 4);
      s.i = v;
    } else {
      std::memcpy(&s.i, p, 8);
    }
    break;
  case ScalarKind::UINT:
    if (s.bytes == 1) {
      uint8_t v;
      std::memcpy(&v, p, 1);
      s.u = v;
    } else if (s.bytes == 2) {
      uint16_t v;
      std::memcpy(&v, p, 2);
      s.u = v;
    } else if (s.bytes == 4) {
      uint32_t v;
      std::memcpy(&v, p, 4);
      s.u = v;
    } else {
      std::memcpy(&s.u, p, 8);
    }
    break;
  case ScalarKind::BOOL:
    s.u = *p != 0;
    break;
  case ScalarKind::OPAQUE:
    break;
  }
  return s;
}

std::string formatFloat(double value, size_t bytes)
{
  if (std::isnan(value))
    return "nan";
  if (std::isinf(value))
    return value < 0 ? "-inf" : "inf";
  if (value == 0.0)
    return std::signbit(value) ? "-0" : "0";

  // The fewest significant digits that read back as the same value
  const int maxDigits = bytes == 8 ? 17 : 9;
  char buf[64];
  for (int digits = 1; digits <= maxDigits; ++digits) {
    std::snprintf(buf, sizeof(buf), "%.*e", digits - 1, value);
    const bool same = bytes == 8 ? std::strtod(buf, nullptr) == value
                                 : std::strtof(buf, nullptr) == float(value);
    if (same)
      break;
  }

  // buf is [-]d[.ddd]e[+-]xx, in the C locale's decimal point: take it apart
  std::string text = buf;
  const char *point = std::localeconv()->decimal_point;
  const std::string decimalPoint = point && point[0] ? point : ".";
  const bool negative = text[0] == '-';
  const auto ePos = text.find('e');
  std::string digits = text.substr(negative ? 1 : 0, ePos - (negative ? 1 : 0));
  const auto pointPos = digits.find(decimalPoint);
  if (pointPos != std::string::npos)
    digits.erase(pointPos, decimalPoint.size());
  const int exponent = std::atoi(text.c_str() + ePos + 1);

  std::string out = negative ? "-" : "";
  const int n = int(digits.size());
  if (exponent >= -5 && exponent < 16) {
    if (exponent < 0) {
      out += "0." + std::string(-exponent - 1, '0') + digits;
    } else if (n <= exponent + 1) {
      out += digits + std::string(exponent + 1 - n, '0');
    } else {
      out += digits.substr(0, exponent + 1) + "." + digits.substr(exponent + 1);
    }
  } else {
    out += digits.substr(0, 1);
    if (n > 1)
      out += "." + digits.substr(1);
    char tail[16];
    std::snprintf(tail,
        sizeof(tail),
        "e%c%02d",
        exponent < 0 ? '-' : '+',
        std::abs(exponent));
    out += tail;
  }
  return out;
}

std::string formatScalar(const Scalar &s)
{
  switch (s.kind) {
  case ScalarKind::FLOAT:
    return formatFloat(s.f, s.bytes);
  case ScalarKind::INT:
    return std::to_string(s.i);
  case ScalarKind::UINT:
    return std::to_string(s.u);
  case ScalarKind::BOOL:
    return s.u ? "true" : "false";
  case ScalarKind::OPAQUE:
    break;
  }
  return "<opaque>";
}

std::string formatTuple(const std::vector<Scalar> &components)
{
  if (components.size() == 1)
    return formatScalar(components[0]);
  std::string text = "(";
  for (size_t c = 0; c < components.size(); ++c) {
    if (c)
      text += ", ";
    text += formatScalar(components[c]);
  }
  return text + ")";
}

std::string formatElement(const ElementLayout &layout, const uint8_t *element)
{
  if (layout.kind == ScalarKind::OPAQUE)
    return "<opaque>";
  std::vector<Scalar> components;
  for (size_t c = 0; c < layout.components; ++c)
    components.push_back(readScalar(layout, element, c));
  return formatTuple(components);
}

std::string quoteString(const std::string &s)
{
  std::string out = "\"";
  for (unsigned char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (c < 0x20 || c == 0x7f) {
        char hex[8];
        std::snprintf(hex, sizeof(hex), "\\x%02x", c);
        out += hex;
      } else {
        out += char(c);
      }
    }
  }
  return out + "\"";
}

const char *objectLabel(ANARIDataType type)
{
  switch (type) {
  case ANARI_ARRAY1D:
    return "array1d";
  case ANARI_ARRAY2D:
    return "array2d";
  case ANARI_ARRAY3D:
    return "array3d";
  case ANARI_CAMERA:
    return "camera";
  case ANARI_FRAME:
    return "frame";
  case ANARI_GEOMETRY:
    return "geometry";
  case ANARI_GROUP:
    return "group";
  case ANARI_INSTANCE:
    return "instance";
  case ANARI_LIGHT:
    return "light";
  case ANARI_MATERIAL:
    return "material";
  case ANARI_RENDERER:
    return "renderer";
  case ANARI_SURFACE:
    return "surface";
  case ANARI_SAMPLER:
    return "sampler";
  case ANARI_SPATIAL_FIELD:
    return "spatialField";
  case ANARI_VOLUME:
    return "volume";
  case ANARI_WORLD:
    return "world";
  default:
    return "object";
  }
}

std::string contentHash(ANARIDataType elementType,
    const std::vector<uint64_t> &dimensions,
    const uint8_t *bytes,
    size_t numBytes)
{
  uint64_t hash = 0xcbf29ce484222325ull;
  auto mix = [&](const void *data, size_t n) {
    const auto *p = (const uint8_t *)data;
    for (size_t i = 0; i < n; ++i) {
      hash ^= p[i];
      hash *= 0x100000001b3ull;
    }
  };
  // Fixed-width little-endian fields, so the hash is the same on every host
  auto mixInt = [&](uint64_t v, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      const uint8_t b = uint8_t(v >> (8 * i));
      mix(&b, 1);
    }
  };
  mixInt(uint64_t(elementType), 4);
  mixInt(dimensions.size(), 4);
  for (auto d : dimensions)
    mixInt(d, 8);
  mix(bytes, numBytes);

  char hex[17];
  std::snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)hash);
  return hex;
}

} // namespace capture_device
