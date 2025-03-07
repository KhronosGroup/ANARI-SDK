// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Orthographic.h"

namespace helide {

Orthographic::Orthographic(HelideGlobalState *s) : Camera(s) {}

void Orthographic::commitParameters()
{
  Camera::commitParameters();
  m_aspect = getParam<float>("aspect", 1.f);
  m_height = getParam<float>("height", 1.f);
}

void Orthographic::finalize()
{
  float2 imgPlaneSize(m_height * m_aspect, m_height);
  m_pos_du = normalize(cross(m_dir, m_up)) * imgPlaneSize.x;
  m_pos_dv = normalize(cross(m_pos_du, m_dir)) * imgPlaneSize.y;
  m_pos_00 = m_pos - .5f * m_pos_du - .5f * m_pos_dv;
}

Ray Orthographic::createRay(const float2 &screen) const
{
  Ray ray;
  ray.dir = m_dir;
  ray.org = m_pos_00 + screen.x * m_pos_du + screen.y * m_pos_dv;
  return ray;
}

} // namespace helide
