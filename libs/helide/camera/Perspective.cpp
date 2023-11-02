// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Perspective.h"

namespace helide {

Perspective::Perspective(HelideGlobalState *s) : Camera(s) {}

void Perspective::commit()
{
  Camera::commit();

  // NOTE: demonstrate alternative 'raw' method for getting parameter values
  float fovy = 0.f;
  if (!getParam("fovy", ANARI_FLOAT32, &fovy))
    fovy = anari::radians(60.f);
  float aspect = getParam<float>("aspect", 1.f);

  float2 imgPlaneSize;
  imgPlaneSize.y = 2.f * tanf(0.5f * fovy);
  imgPlaneSize.x = imgPlaneSize.y * aspect;

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
