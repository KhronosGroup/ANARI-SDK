// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "PBM.h"

namespace helide {

PBM::PBM(HelideGlobalState *s) : Material(s) {}

void PBM::commit()
{
  m_color = getParam<float3>("baseColor", float3(1.f));
  m_colorAttribute = attributeFromString(getParamString("baseColor", "none"));
  m_colorSampler = getParamObject<Sampler>("baseColor");
}

} // namespace helide
