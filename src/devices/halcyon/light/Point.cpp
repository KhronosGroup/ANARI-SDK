// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Point.h"

#include "Light_ispc.h"
// std
#include <algorithm>
#include <cmath>

namespace halcyon {

Point::Point(HalcyonDeviceGlobalState *s) : Light(s) {}

void Point::commitParameters()
{
  commitCommonParameters();
  m_position = getParam<float3>("position", float3(0.f));
  const float radius = getParam<float>("radius", 0.f);
  m_radius = std::isfinite(radius) ? std::max(radius, 0.f) : 0.f;
  commitBrightness(true);
}

void Point::finalize()
{
  // Light.h's table
  float brightness = 0.f;
  if (m_brightness == Brightness::RADIANCE) {
    if (m_radius > 0.f) {
      brightness = m_brightnessValue;
    } else {
      reportMessage(ANARI_SEVERITY_WARNING,
          "point light 'radiance' needs a 'radius' above 0; the light emits "
          "nothing");
    }
  } else {
    // intensity in W/sr; power spreads over the 4 pi sr of the sphere of
    // directions
    const float intensity = m_brightness == Brightness::POWER
        ? m_brightnessValue / (4.f * float(M_PI))
        : m_brightnessValue;
    // a sphere of radiance L radiates L pi r^2 (its projected area) in
    // each direction
    brightness = m_radius > 0.f
        ? intensity / (float(M_PI) * m_radius * m_radius)
        : intensity;
  }
  m_emission = m_color * brightness;
}

bool Point::writeWorldRecord(void *record, const mat4 &xfm) const
{
  if (m_emission == float3(0.f))
    return false;
  const float3 position = linalg::mul(xfm, float4(m_position, 1.f)).xyz();
  if (!std::isfinite(position.x) || !std::isfinite(position.y)
      || !std::isfinite(position.z))
    return false;
  ispc::Light_setPoint(record, &position.x, m_radius, &m_emission.x, m_visible);
  return true;
}

} // namespace halcyon
