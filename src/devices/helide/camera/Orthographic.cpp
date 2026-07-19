// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Orthographic.h"

namespace helide {

Orthographic::Orthographic(HelideGlobalState *s) : Camera(s) {}

void Orthographic::commitParameters()
{
  Camera::commitParameters();
  if (hasParam("aspect", ANARI_FLOAT32))
    m_aspect = getParam<float>("aspect", 1.f);
  else
    m_aspect.reset();
  m_height = getParam<float>("height", 1.f);
}

RayGenerator Orthographic::createRayGenerator(float frameAspect) const
{
  const float aspect = m_aspect.value_or(frameAspect);
  float2 imgPlaneSize(m_height * aspect, m_height);

  RayGenerator rg;
  rg.mode = RayGenerator::Mode::ORTHOGRAPHIC;
  rg.dir = m_dir;
  rg.du = normalize(cross(m_dir, m_up)) * imgPlaneSize.x;
  rg.dv = normalize(cross(rg.du, m_dir)) * imgPlaneSize.y;
  rg.ray_00 = m_pos - .5f * rg.du - .5f * rg.dv;
  return rg;
}

} // namespace helide
