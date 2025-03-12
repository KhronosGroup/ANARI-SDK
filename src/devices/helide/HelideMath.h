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
  uint32_t instID{RTC_INVALID_GEOMETRY_ID};
  uint32_t instArrayID{RTC_INVALID_GEOMETRY_ID};
};

using UniformAttributeSet = std::array<std::optional<float4>, 5>;

inline const std::optional<float4> &getUniformAttribute(
    const UniformAttributeSet &ua, Attribute attr)
{
  return ua[static_cast<int>(attr)];
}

} // namespace helide
