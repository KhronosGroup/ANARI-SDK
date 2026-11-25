// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Camera.h"
#include "OrthographicCamera.h"
#include "PerspectiveCamera.h"
// std
#include <algorithm>
#include <limits>

namespace helide_gpu {

Camera::Camera(HelideGPUDeviceGlobalState *s) : Object(ANARI_CAMERA, s) {}

Camera *Camera::createInstance(
    std::string_view type, HelideGPUDeviceGlobalState *s)
{
  if (type == "perspective")
    return new PerspectiveCamera(s);
  if (type == "orthographic")
    return new OrthographicCamera(s);
  return (Camera *)new UnknownObject(ANARI_CAMERA, s);
}

void Camera::commitParameters()
{
  m_position = getParam<vec3>("position", vec3(0.f, 0.f, 0.f));
  m_direction = getParam<vec3>("direction", vec3(0.f, 0.f, -1.f));
  m_up = getParam<vec3>("up", vec3(0.f, 1.f, 0.f));

  // Unset near and far are fitted to the scene each frame. Set ones must
  // satisfy 0 < near < far (0 <= near for an orthographic camera), or both
  // are ignored.
  m_near.reset();
  m_far.reset();
  const bool hasNear = hasParam("near", ANARI_FLOAT32);
  const bool hasFar = hasParam("far", ANARI_FLOAT32);
  if (!hasNear && !hasFar)
    return;
  const float n = getParam<float>("near", 0.f);
  const float f =
      getParam<float>("far", std::numeric_limits<float>::infinity());
  const bool nearValid = !hasNear || (perspective() ? n > 0.f : n >= 0.f);
  if (!nearValid || !(n < f)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "camera 'near' (%f) and 'far' (%f) must satisfy %s near < far;"
        " ignoring both",
        n,
        f,
        perspective() ? "0 <" : "0 <=");
    return;
  }
  if (hasNear)
    m_near = n;
  if (hasFar)
    m_far = f;
}

mat4 Camera::viewMatrix() const
{
  return glm::lookAtRH(m_position, m_position + m_direction, m_up);
}

ClipRange Camera::clipRange(const box3 &sceneBounds) const
{
  // the range of the bounds' corners' distances along the view direction
  // (inverted for empty bounds)
  float zMin = std::numeric_limits<float>::max();
  float zMax = -std::numeric_limits<float>::max();
  if (sceneBounds.lower.x <= sceneBounds.upper.x) {
    const vec3 dir = glm::normalize(m_direction);
    for (int i = 0; i < 8; ++i) {
      const vec3 corner((i & 1) ? sceneBounds.upper.x : sceneBounds.lower.x,
          (i & 2) ? sceneBounds.upper.y : sceneBounds.lower.y,
          (i & 4) ? sceneBounds.upper.z : sceneBounds.lower.z);
      const float z = glm::dot(corner - m_position, dir);
      zMin = std::min(zMin, z);
      zMax = std::max(zMax, z);
    }
  }
  return fitClipRange(zMin, zMax, perspective(), m_near, m_far);
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_DEFINITION(helide_gpu::Camera *);
