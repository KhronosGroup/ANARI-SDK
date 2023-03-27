// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SciVisVolume.h"

namespace helide {

SciVisVolume::SciVisVolume(HelideGlobalState *d) : Volume(d) {}

void SciVisVolume::commit()
{
  m_field = getParamObject<SpatialField>("field");
  if (!m_field) {
    reportMessage(
        ANARI_SEVERITY_WARNING, "no spatial field provided to scivis volume");
    return;
  }

  m_bounds = m_field ? m_field->bounds() : box3();

  m_valueRange = getParam<box1>("valueRange", box1(0.f, 1.f));
  m_invSize = 1.f / size(m_valueRange);

  m_colorData = getParamObject<Array1D>("color");
  m_opacityData = getParamObject<Array1D>("opacity");
  m_densityScale = getParam<float>("densityScale", 1.f);

  if (!m_colorData) {
    reportMessage(
        ANARI_SEVERITY_WARNING, "no color data provided to scivis volume");
    return;
  }

  if (!m_opacityData) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "no opacity data provided to transfer function");
    return;
  }

  m_colorSampler = ArraySampler1D<float3>(m_colorData.ptr);
  m_opacitySampler = ArraySampler1D<float>(m_opacityData.ptr);
}

bool SciVisVolume::isValid() const
{
  return m_field && m_field->isValid() && m_colorData && m_opacityData;
}

box3 SciVisVolume::bounds() const
{
  return m_bounds;
}

void SciVisVolume::render(const VolumeRay &vray, float3 &color, float &opacity)
{
  const float stepSize = field()->stepSize();
  const float jitter = 1.f; // NOTE: use uniform rng if/when lower sampling rate
  box1 currentInterval = vray.t;
  currentInterval.lower += stepSize * jitter;

  while (opacity < 0.99f && size(currentInterval) >= 0.f) {
    const float3 p = vray.org + vray.dir * currentInterval.lower;
    const float s = position(field()->sampleAt(p), m_valueRange);

    if (!std::isnan(s)) {
      const float3 c = m_colorSampler.valueAt(s);
      const float o = m_opacitySampler.valueAt(s) * m_densityScale;
      accumulateValue(color, c * o, opacity);
      accumulateValue(opacity, o, opacity);
    }

    currentInterval.lower += stepSize;
  }
}

} // namespace helide
