// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "PBM.h"

namespace helide {

PBM::PBM(HelideGlobalState *s) : Material(s) {}

void PBM::commit()
{
  Material::commit();

  m_color = float4(1.f, 1.f, 1.f, 1.f);
  getParam("baseColor", ANARI_FLOAT32_VEC3, &m_color);
  getParam("baseColor", ANARI_FLOAT32_VEC4, &m_color);
  m_colorAttribute =
      attributeFromString(getParamString("baseColor", "none"));
  m_colorSampler = getParamObject<Sampler>("baseColor");

  m_opacity = getParam<float>("opacity", 1.f);
  m_opacityAttribute = attributeFromString(getParamString("opacity", "none"));
  m_opacitySampler = getParamObject<Sampler>("opacity");
}

} // namespace helide
