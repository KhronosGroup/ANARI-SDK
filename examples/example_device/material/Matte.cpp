// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Matte.h"

namespace anari {
namespace example_device {

void Matte::commit()
{
  m_color = getParam<vec3>("color", vec3(.8f,.8f,.8f));
}

vec3 Matte::diffuse() const
{
  return m_color;
}

} // namespace example_device
} // namespace anari
