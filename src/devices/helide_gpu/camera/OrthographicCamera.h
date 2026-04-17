// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Camera.h"

namespace helide_gpu {

struct OrthographicCamera : public Camera
{
  OrthographicCamera(HelideGPUDeviceGlobalState *s);
  ~OrthographicCamera() override = default;

  void commitParameters() override;

  mat4 viewMatrix() const override;
  mat4 projMatrix() const override;

 private:
  vec3 m_position{0.f, 0.f, 0.f};
  vec3 m_direction{0.f, 0.f, -1.f};
  vec3 m_up{0.f, 1.f, 0.f};
  float m_height{1.f};
  float m_aspect{1.f};
  float m_near{0.001f};
  float m_far{1e6f};
};

} // namespace helide_gpu
