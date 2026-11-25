// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"

namespace halcyon {

// The 'directional' light (KHR_LIGHT_DIRECTIONAL): a delta direction, or with
// 'angularDiameter' a cone of directions of uniform radiance (Light.h has the
// units)
struct Directional : public Light
{
  Directional(HalcyonDeviceGlobalState *s);

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

  bool writeWorldRecord(void *record, const mat4 &xfm) const override;

 private:
  // 'direction', normalized; zero if it can't be
  float3 m_direction{0.f, 0.f, -1.f};
  // half of 'angularDiameter', in [0, pi]
  float m_halfAngle{0.f};
  bool m_hasRadiance{false};
  float m_irradiance{1.f};
  float m_radiance{0.f};
  // see Light.h
  float3 m_emission{1.f};
};

} // namespace halcyon
