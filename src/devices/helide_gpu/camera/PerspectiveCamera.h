// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Camera.h"
// std
#include <optional>

namespace helide_gpu {

struct PerspectiveCamera : public Camera
{
  PerspectiveCamera(HelideGPUDeviceGlobalState *s);
  ~PerspectiveCamera() override = default;

  void commitParameters() override;

  bool perspective() const override;
  mat4 projMatrix(float frameAspect, const box3 &sceneBounds) const override;

 private:
  float m_fovy{glm::radians(60.f)};
  std::optional<float> m_aspect;
};

} // namespace helide_gpu
