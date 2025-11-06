// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/helium_math.h"
// embree
#include <embree4/rtcore_common.h>
// std
#include <array>
#include <optional>

namespace helide {

using namespace anari::math;
using namespace helium::math;
using namespace embree_for_helide;

struct Ray
{
  // Ray //

  float3 org;
  float tnear{0.f};
  float3 dir;
  float time{0.f};
  float tfar{std::numeric_limits<float>::max()};
  uint32_t mask{~0u};
  uint32_t id{0u};
  uint32_t flags{0u};

  // Hit //

  float3 Ng;
  float u;
  float v;
  uint32_t primID{RTC_INVALID_GEOMETRY_ID}; // primitive ID
  uint32_t geomID{RTC_INVALID_GEOMETRY_ID}; // geometry ID
  uint32_t instID{RTC_INVALID_GEOMETRY_ID}; // instance ID
  uint32_t instArrayID{RTC_INVALID_GEOMETRY_ID}; // instance sub-array ID
};

struct Volume;
struct VolumeRay
{
  float3 org;
  float3 dir;
  box1 t{0.f, std::numeric_limits<float>::max()};
  Volume *volume{nullptr};
  mat4 invXfm;
  uint32_t instID{RTC_INVALID_GEOMETRY_ID};
  uint32_t instArrayID{RTC_INVALID_GEOMETRY_ID};
};

using UniformAttributeSet = std::array<std::optional<float4>, 5>;

inline const std::optional<float4> &getUniformAttribute(
    const UniformAttributeSet &ua, Attribute attr)
{
  static std::optional<float4> emptyValue;
  switch (attr) {
  case Attribute::NONE:
  case Attribute::WORLD_POSITION:
  case Attribute::WORLD_NORMAL:
  case Attribute::OBJECT_POSITION:
  case Attribute::OBJECT_NORMAL:
    return emptyValue;
  default:
    break;
  }
  return ua[static_cast<int>(attr)];
}

inline std::optional<float4> getRayAttribute(Attribute attr, const Ray &ray)
{
  switch (attr) {
  case Attribute::WORLD_POSITION:
    return float4(ray.org + ray.dir * ray.tfar, 1.f);
  case Attribute::WORLD_NORMAL:
    return float4(normalize(ray.Ng), 1.f);
  case Attribute::OBJECT_POSITION:
    return float4(
        ray.org + ray.dir * ray.tfar, 1.f); // TODO: transform to object space
  case Attribute::OBJECT_NORMAL:
    return float4(normalize(ray.Ng), 1.f); // TODO: transform to object space
  default:
    break;
  }
  return {};
}

inline float3 xfmVec(const mat4 &m, const float3 &p)
{
  return linalg::mul(extractRotation(m), p);
}

inline float3 xfmPoint(const mat4 &m, const float3 &p)
{
  auto r = linalg::mul(m, float4(p.x, p.y, p.z, 1.f));
  return float3(r.x, r.y, r.z);
}

} // namespace helide
