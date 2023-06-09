// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <cstring>
#include <limits>
// embree
#include <embree3/rtcore_common.h>

namespace helide {

// Types //////////////////////////////////////////////////////////////////////

using namespace linalg::aliases;
using mat3 = float3x3;
using mat4 = float4x4;

template <typename T>
struct range_t
{
  using element_t = T;

  range_t() = default;
  range_t(const T &t) : lower(t), upper(t) {}
  range_t(const T &_lower, const T &_upper) : lower(_lower), upper(_upper) {}

  range_t<T> &extend(const T &t)
  {
    lower = min(lower, t);
    upper = max(upper, t);
    return *this;
  }

  range_t<T> &extend(const range_t<T> &t)
  {
    lower = min(lower, t.lower);
    upper = max(upper, t.upper);
    return *this;
  }

  T lower{T(std::numeric_limits<float>::max())};
  T upper{T(-std::numeric_limits<float>::max())};
};

using box1 = range_t<float>;
using box2 = range_t<float2>;
using box3 = range_t<float3>;

template <typename T>
inline typename range_t<T>::element_t size(const range_t<T> &r)
{
  return r.upper - r.lower;
}

template <typename T>
inline typename range_t<T>::element_t clamp(
    const typename range_t<T>::element_t &t, const range_t<T> &r)
{
  return linalg::max(r.lower, linalg::min(t, r.upper));
}

inline float position(float v, const box1 &r)
{
  v = clamp(v, r);
  return (v - r.lower) * (1.f / size(r));
}

struct Ray
{
  // Ray //

  float3 org;
  float tnear{0.f};
  float3 dir;
  float time{0.f};
  float tfar{std::numeric_limits<float>::max()};
  unsigned int mask{~0u};
  unsigned int id{0};
  unsigned int flags{0};

  // Hit //

  float3 Ng;
  float u;
  float v;
  unsigned int primID{RTC_INVALID_GEOMETRY_ID}; // primitive ID
  unsigned int geomID{RTC_INVALID_GEOMETRY_ID}; // geometry ID
  unsigned int instID{RTC_INVALID_GEOMETRY_ID}; // instance ID
};

struct Volume;
struct VolumeRay
{
  float3 org;
  float3 dir;
  box1 t{0.f, std::numeric_limits<float>::max()};
  Volume *volume{nullptr};
};

constexpr float4 DEFAULT_ATTRIBUTE_VALUE(0.f, 0.f, 0.f, 1.f);

enum class Attribute
{
  ATTRIBUTE_0 = 0,
  ATTRIBUTE_1,
  ATTRIBUTE_2,
  ATTRIBUTE_3,
  COLOR,
  NONE
};

enum WrapMode
{
  CLAMP_TO_EDGE = 0,
  REPEAT,
  MIRROR_REPEAT,
  DEFAULT
};

// Functions //////////////////////////////////////////////////////////////////

inline float radians(float degrees)
{
  return degrees * float(M_PI) / 180.f;
}

inline mat3 extractRotation(const mat4 &m)
{
  return mat3(float3(m[0].x, m[0].y, m[0].z),
      float3(m[1].x, m[1].y, m[1].z),
      float3(m[2].x, m[2].y, m[2].z));
}

template <bool SRGB = true>
inline float toneMap(float v)
{
  if constexpr (SRGB)
    return std::pow(v, 1.f / 2.2f);
  else
    return v;
}

struct Interpolant
{
  uint32_t lower;
  uint32_t upper;
  float frac;
};

inline Interpolant getInterpolant(float in, size_t size)
{
  const uint32_t maxIdx = uint32_t(size - 1);
  const float idxf = in * maxIdx;
  const float frac = idxf - std::floor(idxf);
  const uint32_t idx = idxf;
  return {idx, std::min(maxIdx, idx + 1), frac};
}

template <typename T, typename U>
inline T lerp(const T &x, const T &y, const U &a)
{
  return static_cast<T>(x * (U(1) - a) + y * a);
}

template <typename T>
inline void accumulateValue(T &a, const T &b, float interp)
{
  a += b * (1.f - interp);
}

inline int32_t computeMirroredRepeatIndex(int32_t x, int32_t size)
{
  x = std::abs(x + (x < 0)) % (2 * size);
  return x >= size ? 2 * size - x - 1 : x;
};

inline uint32_t calculateWrapIndex(uint32_t i, size_t size, WrapMode wrap)
{
  switch (wrap) {
  case WrapMode::CLAMP_TO_EDGE:
  case WrapMode::DEFAULT:
  default:
    return linalg::clamp(i, 0u, uint32_t(size));
    break;
  case WrapMode::REPEAT:
    return i % size;
    break;
  case WrapMode::MIRROR_REPEAT:
    return uint32_t(computeMirroredRepeatIndex(int32_t(i), size));
    break;
  }
}

inline Attribute attributeFromString(const std::string &str)
{
  if (str == "color")
    return Attribute::COLOR;
  else if (str == "attribute0")
    return Attribute::ATTRIBUTE_0;
  else if (str == "attribute1")
    return Attribute::ATTRIBUTE_1;
  else if (str == "attribute2")
    return Attribute::ATTRIBUTE_2;
  else if (str == "attribute3")
    return Attribute::ATTRIBUTE_3;
  else
    return Attribute::NONE;
}

inline WrapMode wrapModeFromString(const std::string &str)
{
  if (str == "clampToEdge")
    return WrapMode::CLAMP_TO_EDGE;
  else if (str == "repeat")
    return WrapMode::REPEAT;
  else if (str == "mirrorRepeat")
    return WrapMode::MIRROR_REPEAT;
  else
    return WrapMode::DEFAULT;
}

template <typename T>
static const T *typedOffset(const void *mem, uint64_t offset)
{
  return ((const T *)mem) + offset;
}

template <typename ELEMENT_T, int NUM_COMPONENTS, bool SRGB = false>
static float4 getAttributeArrayAt_ufixed(void *data, uint64_t offset)
{
  constexpr float m = std::numeric_limits<ELEMENT_T>::max();
  float4 retval(0.f, 0.f, 0.f, 1.f);
  switch (NUM_COMPONENTS) {
  case 4:
    retval.w = toneMap<SRGB>(
        *typedOffset<ELEMENT_T>(data, NUM_COMPONENTS * offset + 3) / m);
  case 3:
    retval.z = toneMap<SRGB>(
        *typedOffset<ELEMENT_T>(data, NUM_COMPONENTS * offset + 2) / m);
  case 2:
    retval.y = toneMap<SRGB>(
        *typedOffset<ELEMENT_T>(data, NUM_COMPONENTS * offset + 1) / m);
  case 1:
    retval.x = toneMap<SRGB>(
        *typedOffset<ELEMENT_T>(data, NUM_COMPONENTS * offset + 0) / m);
  default:
    break;
  }

  return retval;
}

inline float4 readAsAttributeValueFlat(
    void *data, ANARIDataType type, uint64_t i, size_t size)
{
  auto retval = DEFAULT_ATTRIBUTE_VALUE;

  switch (type) {
  case ANARI_FLOAT32:
    std::memcpy(&retval, typedOffset<float>(data, i), sizeof(float));
    break;
  case ANARI_FLOAT32_VEC2:
    std::memcpy(&retval, typedOffset<float2>(data, i), sizeof(float2));
    break;
  case ANARI_FLOAT32_VEC3:
    std::memcpy(&retval, typedOffset<float3>(data, i), sizeof(float3));
    break;
  case ANARI_FLOAT32_VEC4:
    std::memcpy(&retval, typedOffset<float4>(data, i), sizeof(float4));
    break;
  case ANARI_UFIXED8_R_SRGB:
    retval = getAttributeArrayAt_ufixed<uint8_t, 1, true>(data, i);
    break;
  case ANARI_UFIXED8_RA_SRGB:
    retval = getAttributeArrayAt_ufixed<uint8_t, 2, true>(data, i);
    break;
  case ANARI_UFIXED8_RGB_SRGB:
    retval = getAttributeArrayAt_ufixed<uint8_t, 3, true>(data, i);
    break;
  case ANARI_UFIXED8_RGBA_SRGB:
    retval = getAttributeArrayAt_ufixed<uint8_t, 4, true>(data, i);
    break;
  case ANARI_UFIXED8:
    retval = getAttributeArrayAt_ufixed<uint8_t, 1>(data, i);
    break;
  case ANARI_UFIXED8_VEC2:
    retval = getAttributeArrayAt_ufixed<uint8_t, 2>(data, i);
    break;
  case ANARI_UFIXED8_VEC3:
    retval = getAttributeArrayAt_ufixed<uint8_t, 3>(data, i);
    break;
  case ANARI_UFIXED8_VEC4:
    retval = getAttributeArrayAt_ufixed<uint8_t, 4>(data, i);
    break;
  case ANARI_UFIXED16:
    retval = getAttributeArrayAt_ufixed<uint16_t, 1>(data, i);
    break;
  case ANARI_UFIXED16_VEC2:
    retval = getAttributeArrayAt_ufixed<uint16_t, 2>(data, i);
    break;
  case ANARI_UFIXED16_VEC3:
    retval = getAttributeArrayAt_ufixed<uint16_t, 3>(data, i);
    break;
  case ANARI_UFIXED16_VEC4:
    retval = getAttributeArrayAt_ufixed<uint16_t, 4>(data, i);
    break;
  case ANARI_UFIXED32:
    retval = getAttributeArrayAt_ufixed<uint32_t, 1>(data, i);
    break;
  case ANARI_UFIXED32_VEC2:
    retval = getAttributeArrayAt_ufixed<uint32_t, 2>(data, i);
    break;
  case ANARI_UFIXED32_VEC3:
    retval = getAttributeArrayAt_ufixed<uint32_t, 3>(data, i);
    break;
  case ANARI_UFIXED32_VEC4:
    retval = getAttributeArrayAt_ufixed<uint32_t, 4>(data, i);
    break;
  default:
    break;
  }

  return retval;
}

} // namespace helide

namespace anari {

ANARI_TYPEFOR_SPECIALIZATION(helide::box1, ANARI_FLOAT32_BOX1);

#ifdef HELIDE_ANARI_DEFINITIONS
ANARI_TYPEFOR_DEFINITION(helide::box1);
#endif

} // namespace anari
