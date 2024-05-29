// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "TransferFunction1D.h"

namespace helide {

TransferFunction1D::TransferFunction1D(HelideGlobalState *d)
    : Volume(d), m_field(this)
{}

TransferFunction1D::~TransferFunction1D() = default;

void TransferFunction1D::commit()
{
  Volume::commit();

  m_field = getParamObject<SpatialField>("value");
  if (!m_field) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "no spatial field provided to transferFunction1D volume");
    return;
  }

  m_valueRange = getParam<box1>("valueRange", box1(0.f, 1.f));
  m_invSize = 1.f / size(m_valueRange);

  m_colorData = getParamObject<Array1D>("color");
  m_opacityData = getParamObject<Array1D>("opacity");
  m_densityScale = getParam<float>("densityScale", 1.f);

  if (!m_colorData) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "no color data provided to transferFunction1D volume");
    return;
  }
  if (!m_opacityData) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "no opacity data provided to transfer function");
    return;
  }
}

bool TransferFunction1D::isValid() const
{
  return m_field && m_field->isValid() && m_colorData && m_opacityData;
}

box3 TransferFunction1D::bounds() const
{
  return m_field->bounds();
}

void TransferFunction1D::render(
    const VolumeRay &vray, float3 &color, float &opacity)
{
  const float stepSize = field()->stepSize();
  const float jitter = 1.f; // NOTE: use uniform rng if/when lower sampling rate
  box1 currentInterval = vray.t;
  currentInterval.lower += stepSize * jitter;

  while (opacity < 0.99f && size(currentInterval) >= 0.f) {
    const float3 p = vray.org + vray.dir * currentInterval.lower;
    const float s = field()->sampleAt(p);

    if (!std::isnan(s)) {
      const float3 c = colorOf(s);
      const float o = opacityOf(s) * m_densityScale;
      accumulateValue(color, c * o, opacity);
      accumulateValue(opacity, o, opacity);
    }

    currentInterval.lower += stepSize;
  }
}

} // namespace helide
