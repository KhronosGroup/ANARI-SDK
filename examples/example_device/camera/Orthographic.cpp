// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Orthographic.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace anari {
namespace example_device {

ANARIParameter Orthographic::g_parameters[] = {
  {"position", ANARI_FLOAT32_VEC3},
  {"direction", ANARI_FLOAT32_VEC3},
  {"up", ANARI_FLOAT32_VEC3},
  {"aspect", ANARI_FLOAT32},
  {"height", ANARI_FLOAT32},
  {NULL, ANARI_UNKNOWN},
};

void Orthographic::commit()
{
  Camera::commit();

  const float aspect = getParam<float>("aspect", 1.f);
  const float height = getParam<float>("height", 1.f);

  vec2 imgPlaneSize(height * aspect, height);

  m_pos_du = normalize(cross(m_dir, m_up)) * imgPlaneSize.x;
  m_pos_dv = normalize(cross(m_pos_du, m_dir)) * imgPlaneSize.y;
  m_pos_00 = m_pos - .5f * m_pos_du - .5f * m_pos_dv;
}

Ray Orthographic::createRay(const vec2 &screen) const
{
  Ray ray;

  ray.dir = m_dir;
  ray.org = m_pos_00 + screen.x * m_pos_du + screen.y * m_pos_dv;

  return ray;
}

} // namespace example_device
} // namespace anari
