// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include <anari/anari_cpp.hpp>
#include <anari/anari_cpp/ext/linalg.h>
// std
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

} // namespace helide

namespace anari {

ANARI_TYPEFOR_SPECIALIZATION(helide::box1, ANARI_FLOAT32_BOX1);

#ifdef HELIDE_ANARI_DEFINITIONS
ANARI_TYPEFOR_DEFINITION(helide::box1);
#endif

} // namespace anari
