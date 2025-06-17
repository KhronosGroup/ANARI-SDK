// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "TransferFunction1D.h"
// std
#include <random>

namespace helide {

TransferFunction1D::TransferFunction1D(HelideGlobalState *d)
    : Volume(d), m_field(this), m_colorData(this), m_opacityData(this)
{}

TransferFunction1D::~TransferFunction1D() = default;

void TransferFunction1D::commitParameters()
{
  Volume::commitParameters();
  m_field = getParamObject<SpatialField>("value");
  m_valueRange = getParam<box1>("valueRange", box1(0.f, 1.f));
  double valueRange_d[2] = {0.0, 1.0};
  if (getParam("valueRange", ANARI_FLOAT64_BOX1, &valueRange_d[0])) {
    m_valueRange.lower = float(valueRange_d[0]);
    m_valueRange.upper = float(valueRange_d[1]);
  }
  m_colorData = getParamObject<Array1D>("color");
  m_uniformColor = float4(1.f);
  getParam("color", ANARI_FLOAT32_VEC3, &m_uniformColor);
  getParam("color", ANARI_FLOAT32_VEC4, &m_uniformColor);
  m_opacityData = getParamObject<Array1D>("opacity");
  m_uniformOpacity = getParam<float>("opacity", 1.f) * m_uniformColor.w;
  m_unitDistance = getParam<float>("unitDistance", 1.f);
}

void TransferFunction1D::finalize()
{
  if (!m_field) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "no spatial field provided to transferFunction1D volume");
  }
}

bool TransferFunction1D::isValid() const
{
  return m_field && m_field->isValid();
}

box3 TransferFunction1D::bounds() const
{
  return m_field->bounds();
}

void TransferFunction1D::render(
    const VolumeRay &vray, float invSamplingRate, float3 &color, float &opacity)
{
  const float stepSize = field()->stepSize() * invSamplingRate;
  box1 currentInterval = vray.t;
  std::mt19937 rng;
  rng.seed(vray.t.lower * 10000);
  std::uniform_real_distribution<float> dist(0.f, stepSize);
  currentInterval.lower += dist(rng);

  const float3 org = xfmPoint(vray.invXfm, vray.org);
  const float3 dir = xfmVec(vray.invXfm, vray.dir);

  float transmittance = 1.f;
  while (opacity < 0.99f && size(currentInterval) >= 0.f) {
    const float3 p = org + dir * currentInterval.lower;
    const float s = field()->sampleAt(p);

    if (!std::isnan(s)) {
      const float4 co = colorOf(s);
      const float3 c(co.x, co.y, co.z);
      const float o = opacityOf(s) * co.w;
      const float stepTransmittance =
          std::pow(1.f - o, stepSize / m_unitDistance);
      color += transmittance * (1.f - stepTransmittance) * c;
      opacity += transmittance * (1.f - stepTransmittance);
      transmittance *= stepTransmittance;
    }

    currentInterval.lower += stepSize;
  }
}

} // namespace helide
