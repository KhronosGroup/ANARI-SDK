// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "PerspectiveCamera.h"
// glm
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace helide_gpu {

PerspectiveCamera::PerspectiveCamera(HelideGPUDeviceGlobalState *s) : Camera(s)
{}

void PerspectiveCamera::commitParameters()
{
  m_position = getParam<vec3>("position", vec3(0.f, 0.f, 0.f));
  m_direction = getParam<vec3>("direction", vec3(0.f, 0.f, -1.f));
  m_up = getParam<vec3>("up", vec3(0.f, 1.f, 0.f));
  m_fovy = getParam<float>("fovy", glm::radians(60.f));
  if (hasParam("aspect", ANARI_FLOAT32))
    m_aspect = getParam<float>("aspect", 1.f);
  else
    m_aspect.reset();
  m_near = getParam<float>("near", 0.001f);
  m_far = getParam<float>("far", 1e6f);
}

mat4 PerspectiveCamera::viewMatrix() const
{
  return glm::lookAtRH(m_position, m_position + m_direction, m_up);
}

mat4 PerspectiveCamera::projMatrix(float frameAspect) const
{
  return glm::perspectiveRH_ZO(
      m_fovy, m_aspect.value_or(frameAspect), m_near, m_far);
}

} // namespace helide_gpu
