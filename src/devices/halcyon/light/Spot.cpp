// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Spot.h"

#include "Light_ispc.h"
// std
#include <algorithm>
#include <cmath>

namespace halcyon {

Spot::Spot(HalcyonDeviceGlobalState *s) : Light(s) {}

void Spot::commitParameters()
{
  commitCommonParameters();
  m_position = getParam<float3>("position", float3(0.f));
  m_direction =
      normalizedOrZero(getParam<float3>("direction", float3(0.f, 0.f, -1.f)));
  commitCone();
  commitBrightness(false);
}

void Spot::finalize()
{
  if (!isValid()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "spot light 'direction' is zero or not finite; the light is invalid");
  }
  // Light.h's table: power over the spec's cosine-weighted integral of the
  // cone
  float intensity = m_brightnessValue;
  if (m_brightness == Brightness::POWER) {
    const float W = integrateOverSolidAngle(
        [&](float theta) {
          return coneWeight(theta) * std::max(std::cos(theta), 0.f);
        },
        0.f,
        std::min(m_coneAngle, float(M_PI) / 2.f));
    intensity = W > 0.f ? m_brightnessValue / W : 0.f;
  }
  m_emission = m_color * intensity;
}

bool Spot::isValid() const
{
  return m_direction != float3(0.f);
}

bool Spot::writeWorldRecord(void *record, const mat4 &xfm) const
{
  if (m_emission == float3(0.f) || m_coneAngle <= 0.f)
    return false;
  const float3 position = linalg::mul(xfm, float4(m_position, 1.f)).xyz();
  const float3 direction = linalg::mul(xfm, float4(m_direction, 0.f)).xyz();
  const float3 axis = normalizedOrZero(direction);
  if (!isFinite(position) || axis == float3(0.f))
    return false;
  ispc::Light_setSpot(record,
      &position.x,
      &axis.x,
      m_coneAngle,
      m_coneSoftAngle,
      &m_emission.x,
      m_visible);
  return true;
}

} // namespace halcyon
