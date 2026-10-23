// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "QuadLight.h"

#include "Light_ispc.h"
// std
#include <algorithm>
#include <cmath>

namespace halcyon {

QuadLight::QuadLight(HalcyonDeviceGlobalState *s) : Light(s) {}

void QuadLight::commitParameters()
{
  commitCommonParameters();
  m_position = getParam<float3>("position", float3(0.f));
  m_edge1 = getParam<float3>("edge1", float3(1.f, 0.f, 0.f));
  m_edge2 = getParam<float3>("edge2", float3(0.f, 1.f, 0.f));
  const std::string side = getParamString("side", "front");
  m_front = side != "back";
  m_back = side == "back" || side == "both";
  if (side != "front" && side != "back" && side != "both") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "quad light 'side' '%s' is not 'front', 'back' or 'both'; 'front' "
        "is used",
        side.c_str());
  }
  commitBrightness(true);
  commitDistribution();
}

void QuadLight::finalize()
{
  if (!isValid()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "quad light 'edge1' and 'edge2' span no area; the light is invalid");
  }
  finalizeDistribution();
  // Light.h's W: gamma is measured from the axis, the normal of the front
  // (or of the back if only it emits); one side emits into gamma up to
  // pi / 2, both into every direction
  const float gammaMax = m_front && m_back ? float(M_PI) : float(M_PI) / 2.f;
  if (m_distribution.empty()) {
    m_powerPerIntensity = float(M_PI) * (m_front && m_back ? 2.f : 1.f);
  } else {
    m_powerPerIntensity = integrateOverSolidAngle(
        [&](float gamma) { return distributionMeanOverC(gamma); },
        0.f,
        gammaMax);
  }
}

bool QuadLight::isValid() const
{
  const float area = linalg::length(linalg::cross(m_edge1, m_edge2));
  return std::isfinite(area) && area > 0.f && isFinite(m_position);
}

bool QuadLight::writeWorldRecord(void *record, const mat4 &xfm) const
{
  // the edges are vectors: the transform maps them whole, scale included
  const float3 position = linalg::mul(xfm, float4(m_position, 1.f)).xyz();
  const float3 edge1 = linalg::mul(xfm, float4(m_edge1, 0.f)).xyz();
  const float3 edge2 = linalg::mul(xfm, float4(m_edge2, 0.f)).xyz();
  const float3 normal = linalg::cross(edge2, edge1);
  const float area = linalg::length(normal);
  const float edge1Length = linalg::length(edge1);
  if (!isFinite(position) || !std::isfinite(area) || area <= 0.f
      || !std::isfinite(edge1Length) || edge1Length <= 0.f)
    return false;

  // Light.h's table, with the area after the transform
  float radiance = m_brightnessValue;
  if (m_brightness == Brightness::INTENSITY)
    radiance = m_brightnessValue / area;
  else if (m_brightness == Brightness::POWER) {
    radiance = m_powerPerIntensity > 0.f
        ? m_brightnessValue / (m_powerPerIntensity * area)
        : 0.f;
  }
  const float3 emission = m_color * radiance;
  if (emission == float3(0.f) || !isFinite(emission))
    return false;

  const float3 front = normal / area;
  const float3 axis = m_front ? front : -front;
  // the C0 half-plane is aligned with edge1; C grows right-handed about the
  // axis
  const float3 c0 = edge1 / edge1Length;
  const float3 c90 = linalg::cross(axis, c0);
  ispc::Light_setQuad(record,
      &position.x,
      &edge1.x,
      &edge2.x,
      &axis.x,
      area,
      m_front,
      m_back,
      &c0.x,
      &c90.x,
      m_distribution.empty() ? nullptr : m_distribution.data(),
      m_distributionNumGamma,
      m_distributionNumC,
      &emission.x,
      m_visible);
  return true;
}

} // namespace halcyon
