// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "OrthographicCamera.h"
// glm
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace helide_gpu {

OrthographicCamera::OrthographicCamera(HelideGPUDeviceGlobalState *s) : Camera(s)
{}

void OrthographicCamera::commitParameters()
{
  m_position = getParam<vec3>("position", vec3(0.f, 0.f, 0.f));
  m_direction = getParam<vec3>("direction", vec3(0.f, 0.f, -1.f));
  m_up = getParam<vec3>("up", vec3(0.f, 1.f, 0.f));
  m_height = getParam<float>("height", 1.f);
  m_aspect = getParam<float>("aspect", 1.f);
  m_near = getParam<float>("near", 0.001f);
  m_far = getParam<float>("far", 1e6f);
}

mat4 OrthographicCamera::viewMatrix() const
{
  return glm::lookAtRH(m_position, m_position + m_direction, m_up);
}

mat4 OrthographicCamera::projMatrix() const
{
  const float halfH = m_height * 0.5f;
  const float halfW = halfH * m_aspect;
  return glm::orthoRH_ZO(-halfW, halfW, -halfH, halfH, m_near, m_far);
}

} // namespace helide_gpu
