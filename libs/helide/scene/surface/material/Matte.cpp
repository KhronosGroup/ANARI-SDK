// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Matte.h"

namespace helide {

Matte::Matte(HelideGlobalState *s) : Material(s) {}

void Matte::commit()
{
  m_color = getParam<float3>("color", float3(1.f));
  m_opacity = 1.f;
}

} // namespace helide
