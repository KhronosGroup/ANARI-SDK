// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "TransparentMatte.h"

namespace helide {

TransparentMatte::TransparentMatte(HelideGlobalState *s) : Material(s) {}

void TransparentMatte::commit()
{
  m_color = getParam<float3>("color", float3(1.f));
  m_opacity = getParam<float>("opacity", 1.f);

  m_colorAttribute = attributeFromString(getParamString("color", "none"));
  m_opacityAttribute = attributeFromString(getParamString("opacity", "none"));
}

} // namespace helide
