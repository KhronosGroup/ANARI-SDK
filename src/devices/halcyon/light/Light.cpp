// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Light.h"
#include "Directional.h"
#include "Hdri.h"
#include "Point.h"
#include "QuadLight.h"
#include "Ring.h"
#include "Spot.h"
// std
#include <algorithm>
#include <cmath>

namespace halcyon {

Light::Light(HalcyonDeviceGlobalState *s) : Object(ANARI_LIGHT, s) {}

Light *Light::createInstance(
    std::string_view subtype, HalcyonDeviceGlobalState *s)
{
  if (subtype == "directional")
    return new Directional(s);
  if (subtype == "point")
    return new Point(s);
  if (subtype == "spot")
    return new Spot(s);
  if (subtype == "quad")
    return new QuadLight(s);
  if (subtype == "ring")
    return new Ring(s);
  if (subtype == "hdri")
    return new Hdri(s);
  return (Light *)new UnknownObject(ANARI_LIGHT, s);
}

void Light::commitCommonParameters()
{
  m_color = linalg::max(getParam<float3>("color", float3(1.f)), float3(0.f));
  m_visible = getParam<bool>("visible", true);
}

void Light::commitBrightness(bool radiance)
{
  // the spec's precedence: the first of 'intensity', 'power', 'radiance' set
  const bool hasPower = hasParam("power", ANARI_FLOAT32);
  const bool hasRadiance = radiance && hasParam("radiance", ANARI_FLOAT32);
  if (hasParam("intensity", ANARI_FLOAT32) || !(hasPower || hasRadiance)) {
    m_brightness = Brightness::INTENSITY;
    m_brightnessValue = getParam<float>("intensity", 1.f);
  } else if (hasPower) {
    m_brightness = Brightness::POWER;
    m_brightnessValue = getParam<float>("power", 1.f);
  } else {
    m_brightness = Brightness::RADIANCE;
    m_brightnessValue = getParam<float>("radiance", 1.f);
  }
  m_brightnessValue =
      std::isfinite(m_brightnessValue) ? std::max(m_brightnessValue, 0.f) : 0.f;
}

void Light::commitCone()
{
  float openingAngle = getParam<float>("openingAngle", float(M_PI));
  openingAngle = std::isfinite(openingAngle)
      ? std::clamp(openingAngle, 0.f, 2.f * float(M_PI))
      : float(M_PI);
  float falloffAngle = getParam<float>("falloffAngle", 0.1f);
  falloffAngle = std::isfinite(falloffAngle) ? falloffAngle : 0.1f;
  m_coneAngle = 0.5f * openingAngle;
  m_coneSoftAngle = m_coneAngle - std::clamp(falloffAngle, 0.f, m_coneAngle);
}

void Light::commitDistribution()
{
  m_distribution1D = getParamObject<Array1D>("intensityDistribution");
  m_distribution2D = getParamObject<Array2D>("intensityDistribution");
}

void Light::finalizeDistribution()
{
  m_distribution.clear();
  m_distributionNumGamma = 0;
  m_distributionNumC = 0;

  const helium::Array *array = nullptr;
  const float *values = nullptr;
  size_t numGamma = 0;
  size_t numC = 1;
  if (m_distribution1D) {
    array = m_distribution1D.get();
    numGamma = m_distribution1D->size();
    if (array->elementType() == ANARI_FLOAT32)
      values = m_distribution1D->beginAs<float>();
  } else if (m_distribution2D) {
    array = m_distribution2D.get();
    numGamma = m_distribution2D->size().x;
    numC = m_distribution2D->size().y;
    if (array->elementType() == ANARI_FLOAT32)
      values = m_distribution2D->dataAs<float>();
  } else {
    if (hasParam("intensityDistribution")) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "light 'intensityDistribution' must be an ARRAY1D or ARRAY2D; "
          "it is ignored");
    }
    return;
  }

  if (!values) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "light 'intensityDistribution' must have FLOAT32 elements (got %s); "
        "it is ignored",
        anari::toString(array->elementType()));
    return;
  }
  if (numGamma < 2 || numC < 1) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "light 'intensityDistribution' needs at least two values over gamma "
        "(got %zu); it is ignored",
        numGamma);
    return;
  }

  m_distribution.assign(values, values + numGamma * numC);
  bool clamped = false;
  for (auto &v : m_distribution) {
    if (!(v >= 0.f) || !std::isfinite(v)) {
      v = std::isfinite(v) ? std::max(v, 0.f) : 0.f;
      clamped = true;
    }
  }
  if (clamped) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "light 'intensityDistribution' has negative or non-finite values; "
        "they are taken as 0");
  }
  m_distributionNumGamma = uint32_t(numGamma);
  m_distributionNumC = uint32_t(numC);
}

bool Light::isFinite(const float3 &v)
{
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

float3 Light::normalizedOrZero(const float3 &v)
{
  const float len = linalg::length(v);
  return std::isfinite(len) && len > 0.f ? v / len : float3(0.f);
}

float Light::coneWeight(float theta) const
{
  // Ported from Typhoon's _Smoothstep(theta, coneSoftAngle, coneAngle)
  // (delegate/light.cpp, as Lights_coneWeight() in Lights.isph; credit there
  // and in THIRD-PARTY.md)
  const float length = m_coneAngle - m_coneSoftAngle;
  float smooth = 0.f;
  if (length == 0.f) {
    smooth = theta <= m_coneSoftAngle ? 0.f : 1.f;
  } else {
    const float t = std::clamp((theta - m_coneSoftAngle) / length, 0.f, 1.f);
    smooth = t * t * (3.f - 2.f * t);
  }
  return 1.f - smooth;
}

float Light::distributionMeanOverC(float gamma) const
{
  if (m_distribution.empty())
    return 0.f;
  // linear in gamma between the values (Lights.isph), and the C half-planes
  // interpolate linearly and periodically, so the mean over C is that of
  // the rows
  const uint32_t n = m_distributionNumGamma;
  const float u = std::clamp(gamma / float(M_PI), 0.f, 1.f) * float(n - 1);
  const uint32_t i0 = std::min(uint32_t(u), n - 2);
  const float f = u - float(i0);
  double sum = 0.0;
  for (uint32_t c = 0; c < m_distributionNumC; c++) {
    const float *row = m_distribution.data() + size_t(c) * n;
    sum += (1.f - f) * row[i0] + f * row[i0 + 1];
  }
  return float(sum / m_distributionNumC);
}

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_DEFINITION(halcyon::Light *);
