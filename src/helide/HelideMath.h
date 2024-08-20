// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/helium_math.h"
// embree
#include <embree3/rtcore_common.h>
// std
#include <array>
#include <optional>

namespace helide {

using namespace anari::math;
using namespace helium::math;

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
  uint32_t instID{RTC_INVALID_GEOMETRY_ID};
};

using UniformAttributeSet = std::array<std::optional<float4>, 5>;

inline const std::optional<float4> &getUniformAttribute(
    const UniformAttributeSet &ua, Attribute attr)
{
  return ua[static_cast<int>(attr)];
}

} // namespace helide
