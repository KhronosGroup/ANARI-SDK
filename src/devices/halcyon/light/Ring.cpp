// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Ring.h"

#include "Light_ispc.h"
// std
#include <algorithm>
#include <cmath>

namespace halcyon {

namespace {

float sizeParam(float v)
{
  return std::isfinite(v) ? std::max(v, 0.f) : 0.f;
}

// A unit vector perpendicular to unit 'axis': 'v' (of any length; zero if
// not usable) made unit and projected, if it isn't along the axis
float3 perpendicular(const float3 &vIn, const float3 &axis)
{
  const float vLen = linalg::length(vIn);
  const float3 v = std::isfinite(vLen) && vLen > 0.f ? vIn / vLen : float3(0.f);
  float3 p = v - linalg::dot(v, axis) * axis;
  float len = linalg::length(p);
  if (std::isfinite(len) && len > 1e-6f)
    return p / len;
  // any other: the axis's smallest component's direction, projected
  const float3 a = linalg::abs(axis);
  const float3 e = a.x <= a.y && a.x <= a.z ? float3(1.f, 0.f, 0.f)
      : a.y <= a.z                          ? float3(0.f, 1.f, 0.f)
                                            : float3(0.f, 0.f, 1.f);
  p = e - linalg::dot(e, axis) * axis;
  return p / linalg::length(p);
}

} // namespace

Ring::Ring(HalcyonDeviceGlobalState *s) : Light(s) {}

void Ring::commitParameters()
{
  commitCommonParameters();
  m_position = getParam<float3>("position", float3(0.f));
  m_direction =
      normalizedOrZero(getParam<float3>("direction", float3(0.f, 0.f, -1.f)));
  m_c0 = getParam<float3>("c0", float3(1.f, 0.f, 0.f));
  m_radius = sizeParam(getParam<float>("radius", 0.f));
  m_innerRadius = sizeParam(getParam<float>("innerRadius", 0.f));
  commitCone();
  commitBrightness(true);
  commitDistribution();
}

void Ring::finalize()
{
  if (!isValid()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "ring light 'direction' is zero or not finite; the light is invalid");
  }
  // the radii as sampled: an innerRadius not below radius is ignored
  m_sampledInnerRadius = m_innerRadius < m_radius ? m_innerRadius : 0.f;
  m_area = float(M_PI)
      * (m_radius * m_radius - m_sampledInnerRadius * m_sampledInnerRadius);
  if (m_innerRadius > 0.f && m_innerRadius >= m_radius && m_radius > 0.f) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "ring light 'innerRadius' must be smaller than 'radius'; the ring is "
        "a disk");
  }
  finalizeDistribution();
  if (!m_distribution.empty() && m_distributionNumC > 1 && isValid()
      && linalg::length(linalg::cross(normalizedOrZero(m_c0), m_direction))
          <= 1e-6f) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "ring light 'c0' is zero or parallel to 'direction'; the C0 "
        "half-plane of 'intensityDistribution' is another direction");
  }

  // Light.h's table: the intensity along the axis I, then the radiance
  // I / A of a ring
  const float area = m_area;
  float intensity = m_brightnessValue;
  if (m_brightness == Brightness::RADIANCE) {
    if (m_radius > 0.f) {
      intensity = m_brightnessValue * area;
    } else {
      reportMessage(ANARI_SEVERITY_WARNING,
          "ring light 'radiance' needs a 'radius' above 0; the light emits "
          "nothing");
      intensity = 0.f;
    }
  } else if (m_brightness == Brightness::POWER) {
    // the profile over the front hemisphere: the cone times the
    // distribution, or the cosine of a Lambertian emitter
    const float W = integrateOverSolidAngle(
        [&](float gamma) {
          return coneWeight(gamma)
              * (m_distribution.empty() ? std::cos(gamma)
                                        : distributionMeanOverC(gamma));
        },
        0.f,
        float(M_PI) / 2.f);
    intensity = W > 0.f ? m_brightnessValue / W : 0.f;
  }
  const float brightness =
      m_radius > 0.f ? (area > 0.f ? intensity / area : 0.f) : intensity;
  m_emission = m_color * (std::isfinite(brightness) ? brightness : 0.f);
}

bool Ring::isValid() const
{
  return m_direction != float3(0.f);
}

bool Ring::writeWorldRecord(void *record, const mat4 &xfm) const
{
  if (m_emission == float3(0.f) || m_coneAngle <= 0.f)
    return false;
  const float3 position = linalg::mul(xfm, float4(m_position, 1.f)).xyz();
  const float3 direction = linalg::mul(xfm, float4(m_direction, 0.f)).xyz();
  const float3 c0In = linalg::mul(xfm, float4(m_c0, 0.f)).xyz();
  const float3 axis = normalizedOrZero(direction);
  if (!isFinite(position) || axis == float3(0.f))
    return false;
  // the frame of the disk: x is 'c0' in its plane, and x x y = -axis, as
  // Typhoon's disk light emits along its local -z; C grows right-handed
  // about the axis
  const float3 c0 = perpendicular(isFinite(c0In) ? c0In : float3(0.f), axis);
  const float3 frameY = linalg::cross(c0, axis);
  const float3 c90 = linalg::cross(axis, c0);
  const float fraction = m_radius > 0.f ? m_sampledInnerRadius / m_radius : 0.f;
  ispc::Light_setRing(record,
      &position.x,
      &axis.x,
      &c0.x,
      &frameY.x,
      m_radius,
      fraction * fraction,
      m_area,
      m_coneAngle,
      m_coneSoftAngle,
      &c0.x,
      &c90.x,
      m_distribution.empty() ? nullptr : m_distribution.data(),
      m_distributionNumGamma,
      m_distributionNumC,
      &m_emission.x,
      m_visible);
  return true;
}

} // namespace halcyon
