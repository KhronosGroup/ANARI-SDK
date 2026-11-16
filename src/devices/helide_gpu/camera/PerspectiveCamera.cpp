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
  Camera::commitParameters();
  m_fovy = getParam<float>("fovy", glm::radians(60.f));
  if (hasParam("aspect", ANARI_FLOAT32))
    m_aspect = getParam<float>("aspect", 1.f);
  else
    m_aspect.reset();
}

bool PerspectiveCamera::perspective() const
{
  return true;
}

mat4 PerspectiveCamera::projMatrix(
    float frameAspect, const box3 &sceneBounds) const
{
  const ClipRange clip = clipRange(sceneBounds);
  return glm::perspectiveRH_ZO(
      m_fovy, m_aspect.value_or(frameAspect), clip.zNear, clip.zFar);
}

} // namespace helide_gpu
