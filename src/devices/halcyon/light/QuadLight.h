// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"

namespace halcyon {

// The 'quad' light (KHR_LIGHT_QUAD): a parallelogram of uniform radiance (a
// Lambertian emitter) or of 'intensityDistribution', emitting from its
// front (towards edge2 x edge1, the spec's), its back or both (Light.h has
// the units). Not 'Quad': that is the quad geometry (Geometry.cpp), in the
// same namespace.
struct QuadLight : public Light
{
  QuadLight(HalcyonDeviceGlobalState *s);

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

  bool writeWorldRecord(void *record, const mat4 &xfm) const override;

 private:
  float3 m_position{0.f};
  float3 m_edge1{1.f, 0.f, 0.f};
  float3 m_edge2{0.f, 1.f, 0.f};
  bool m_front{true};
  bool m_back{false};
  // the power per unit of intensity along the axis (W in Light.h)
  float m_powerPerIntensity{float(M_PI)};
};

} // namespace halcyon
