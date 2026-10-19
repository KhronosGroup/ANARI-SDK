// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Directional.h"

#include "Light_ispc.h"
// std
#include <algorithm>
#include <cmath>

namespace halcyon {

Directional::Directional(HalcyonDeviceGlobalState *s) : Light(s) {}

void Directional::commitParameters()
{
  commitCommonParameters();
  m_direction =
      normalizedOrZero(getParam<float3>("direction", float3(0.f, 0.f, -1.f)));
  const float angularDiameter = getParam<float>("angularDiameter", 0.f);
  m_halfAngle = std::isfinite(angularDiameter)
      ? std::clamp(0.5f * angularDiameter, 0.f, float(M_PI))
      : 0.f;
  // the spec: 'irradiance' wins when both are set
  m_hasRadiance = !hasParam("irradiance", ANARI_FLOAT32)
      && hasParam("radiance", ANARI_FLOAT32);
  m_irradiance = std::max(getParam<float>("irradiance", 1.f), 0.f);
  m_radiance = std::max(getParam<float>("radiance", 0.f), 0.f);
}

void Directional::finalize()
{
  if (!isValid()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "directional light 'direction' is zero or not finite; the light is "
        "invalid");
  }
  // Light.h's table
  float brightness = 0.f;
  if (m_hasRadiance) {
    if (m_halfAngle > 0.f) {
      brightness = m_radiance;
    } else {
      reportMessage(ANARI_SEVERITY_WARNING,
          "directional light 'radiance' needs an 'angularDiameter' above 0; "
          "the light emits nothing");
    }
  } else if (m_halfAngle > 0.f) {
    // the cosine-weighted solid angle of the cone, as seen from a surface
    // facing the light; where the cone covers the surface's hemisphere, the
    // hemisphere's
    const float s = std::sin(std::min(m_halfAngle, float(M_PI) / 2.f));
    brightness = m_irradiance / (float(M_PI) * s * s);
  } else {
    brightness = m_irradiance;
  }
  m_emission = m_color * brightness;
}

bool Directional::isValid() const
{
  return m_direction != float3(0.f);
}

bool Directional::writeWorldRecord(void *record, const mat4 &xfm) const
{
  if (m_emission == float3(0.f))
    return false;
  const float3 direction = linalg::mul(xfm, float4(m_direction, 0.f)).xyz();
  const float len = linalg::length(direction);
  if (!std::isfinite(len) || len <= 0.f)
    return false;
  const float3 axis = -direction / len;
  ispc::Light_setDirectional(
      record, &axis.x, m_halfAngle, &m_emission.x, m_visible);
  return true;
}

} // namespace halcyon
