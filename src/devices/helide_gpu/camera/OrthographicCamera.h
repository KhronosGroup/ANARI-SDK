// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Camera.h"
// std
#include <optional>

namespace helide_gpu {

struct OrthographicCamera : public Camera
{
  OrthographicCamera(HelideGPUDeviceGlobalState *s);
  ~OrthographicCamera() override = default;

  void commitParameters() override;

  bool perspective() const override;
  mat4 projMatrix(float frameAspect, const box3 &sceneBounds) const override;

 private:
  float m_height{1.f};
  std::optional<float> m_aspect;
};

} // namespace helide_gpu
