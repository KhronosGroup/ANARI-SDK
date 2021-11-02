// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "OBJ.h"

namespace anari {
namespace example_device {

void OBJ::commit()
{
  m_Kd = getParam<vec3>("kd", vec3(1.f));
}

vec3 OBJ::diffuse() const
{
  return m_Kd;
}

} // namespace example_device
} // namespace anari
