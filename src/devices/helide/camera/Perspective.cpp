// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Perspective.h"

namespace helide {

Perspective::Perspective(HelideGlobalState *s) : Camera(s) {}

void Perspective::commitParameters()
{
  Camera::commitParameters();
  // NOTE: demonstrate alternative 'raw' method for getting parameter values
  if (!getParam("fovy", ANARI_FLOAT32, &m_fovy))
    m_fovy = anari::radians(60.f);
  if (hasParam("aspect", ANARI_FLOAT32))
    m_aspect = getParam<float>("aspect", 1.f);
  else
    m_aspect.reset();
}

RayGenerator Perspective::createRayGenerator(float frameAspect) const
{
  const float aspect = m_aspect.value_or(frameAspect);
  float2 imgPlaneSize;
  imgPlaneSize.y = 2.f * tanf(0.5f * m_fovy);
  imgPlaneSize.x = imgPlaneSize.y * aspect;

  RayGenerator rg;
  rg.mode = RayGenerator::Mode::PERSPECTIVE;
  rg.org = m_pos;
  rg.du = normalize(cross(m_dir, m_up)) * imgPlaneSize.x;
  rg.dv = normalize(cross(rg.du, m_dir)) * imgPlaneSize.y;
  rg.ray_00 = m_dir - .5f * rg.du - .5f * rg.dv;
  return rg;
}

} // namespace helide
