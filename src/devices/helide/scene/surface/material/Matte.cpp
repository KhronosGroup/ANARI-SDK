// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Matte.h"

namespace helide {

Matte::Matte(HelideGlobalState *s) : Material(s) {}

void Matte::commit()
{
  Material::commit();

  m_color = float4(1.f, 1.f, 1.f, 1.f);
  getParam("color", ANARI_FLOAT32_VEC3, &m_color);
  getParam("color", ANARI_FLOAT32_VEC4, &m_color);
  m_colorAttribute = attributeFromString(getParamString("color", "none"));
  m_colorSampler = getParamObject<Sampler>("color");

  m_opacity = getParam<float>("opacity", 1.f);
  m_opacityAttribute = attributeFromString(getParamString("opacity", "none"));
  m_opacitySampler = getParamObject<Sampler>("opacity");
}

} // namespace helide
