// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Perspective.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace anari {
namespace example_device {

void Perspective::commit()
{
  Camera::commit();

  float fovy = getParam<float>("fovy", glm::radians(60.f));
  float aspect = getParam<float>("aspect", 1.f);

  vec2 imgPlaneSize;
  imgPlaneSize.y = 2.f * tanf(0.5f * fovy);
  imgPlaneSize.x = imgPlaneSize.y * aspect;

  m_dir_du = normalize(cross(m_dir, m_up)) * imgPlaneSize.x;
  m_dir_dv = normalize(cross(m_dir_du, m_dir)) * imgPlaneSize.y;
  m_dir_00 = m_dir - .5f * m_dir_du - .5f * m_dir_dv;
}

Ray Perspective::createRay(const vec2 &screen) const
{
  Ray ray;

  ray.org = m_pos;
  ray.dir = normalize(m_dir_00 + screen.x * m_dir_du + screen.y * m_dir_dv);

  return ray;
}

} // namespace example_device
} // namespace anari
