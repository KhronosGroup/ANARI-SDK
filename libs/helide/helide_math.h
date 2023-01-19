// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// linalg
#include "external/linalg.h"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <limits>

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

} // namespace helide

namespace anari {

ANARI_TYPEFOR_SPECIALIZATION(helide::float2, ANARI_FLOAT32_VEC2);
ANARI_TYPEFOR_SPECIALIZATION(helide::float3, ANARI_FLOAT32_VEC3);
ANARI_TYPEFOR_SPECIALIZATION(helide::float4, ANARI_FLOAT32_VEC4);
ANARI_TYPEFOR_SPECIALIZATION(helide::int2, ANARI_INT32_VEC2);
ANARI_TYPEFOR_SPECIALIZATION(helide::int3, ANARI_INT32_VEC3);
ANARI_TYPEFOR_SPECIALIZATION(helide::int4, ANARI_INT32_VEC4);
ANARI_TYPEFOR_SPECIALIZATION(helide::uint2, ANARI_UINT32_VEC2);
ANARI_TYPEFOR_SPECIALIZATION(helide::uint3, ANARI_UINT32_VEC3);
ANARI_TYPEFOR_SPECIALIZATION(helide::uint4, ANARI_UINT32_VEC4);
ANARI_TYPEFOR_SPECIALIZATION(helide::mat4, ANARI_FLOAT32_MAT4);

#ifdef HELIDE_ANARI_DEFINITIONS
ANARI_TYPEFOR_DEFINITION(helide::float2);
ANARI_TYPEFOR_DEFINITION(helide::float3);
ANARI_TYPEFOR_DEFINITION(helide::float4);
ANARI_TYPEFOR_DEFINITION(helide::int2);
ANARI_TYPEFOR_DEFINITION(helide::int3);
ANARI_TYPEFOR_DEFINITION(helide::int4);
ANARI_TYPEFOR_DEFINITION(helide::uint2);
ANARI_TYPEFOR_DEFINITION(helide::uint3);
ANARI_TYPEFOR_DEFINITION(helide::uint4);
ANARI_TYPEFOR_DEFINITION(helide::mat4);
#endif

} // namespace anari
