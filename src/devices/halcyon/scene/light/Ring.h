// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"

namespace halcyon {

// The 'ring' light (KHR_LIGHT_RING): a disk, or with 'innerRadius' an
// annulus, of 'radius' around 'position' facing 'direction', emitting from
// that side into a cone as a Lambertian emitter or by
// 'intensityDistribution'; with 'radius' 0 a delta point emitting as a
// vanishing disk would (Light.h has the units and the cone)
struct Ring : public Light
{
  Ring(HalcyonDeviceGlobalState *s);

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

  bool writeWorldRecord(void *record, const mat4 &xfm) const override;

 private:
  float3 m_position{0.f};
  // 'direction', normalized; zero if it can't be
  float3 m_direction{0.f, 0.f, -1.f};
  float3 m_c0{1.f, 0.f, 0.f};
  float m_radius{0.f};
  float m_innerRadius{0.f};
  // 'innerRadius' if below 'radius', else 0; the area between
  float m_sampledInnerRadius{0.f};
  float m_area{0.f};
  // see Light.h: the radiance of a ring, the intensity along the axis of a
  // delta point, times 'color'
  float3 m_emission{1.f};
};

} // namespace halcyon
