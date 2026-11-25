// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "OrthographicCamera.h"
// glm
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace helide_gpu {

OrthographicCamera::OrthographicCamera(HelideGPUDeviceGlobalState *s)
    : Camera(s)
{}

void OrthographicCamera::commitParameters()
{
  Camera::commitParameters();
  m_height = getParam<float>("height", 1.f);
  if (hasParam("aspect", ANARI_FLOAT32))
    m_aspect = getParam<float>("aspect", 1.f);
  else
    m_aspect.reset();
}

bool OrthographicCamera::perspective() const
{
  return false;
}

mat4 OrthographicCamera::projMatrix(
    float frameAspect, const box3 &sceneBounds) const
{
  const ClipRange clip = clipRange(sceneBounds);
  const float aspect = m_aspect.value_or(frameAspect);
  const float halfH = m_height * 0.5f;
  const float halfW = halfH * aspect;
  return glm::orthoRH_ZO(-halfW, halfW, -halfH, halfH, clip.zNear, clip.zFar);
}

} // namespace helide_gpu
