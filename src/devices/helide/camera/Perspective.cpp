// Copyright 2022-2024 The Khronos Group
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
  m_aspect = getParam<float>("aspect", 1.f);
}

void Perspective::finalize()
{
  float2 imgPlaneSize;
  imgPlaneSize.y = 2.f * tanf(0.5f * m_fovy);
  imgPlaneSize.x = imgPlaneSize.y * m_aspect;
  m_dir_du = normalize(cross(m_dir, m_up)) * imgPlaneSize.x;
  m_dir_dv = normalize(cross(m_dir_du, m_dir)) * imgPlaneSize.y;
  m_dir_00 = m_dir - .5f * m_dir_du - .5f * m_dir_dv;
}

Ray Perspective::createRay(const float2 &screen) const
{
  Ray ray;
  ray.org = m_pos;
  ray.dir = normalize(m_dir_00 + screen.x * m_dir_du + screen.y * m_dir_dv);
  return ray;
}

} // namespace helide
