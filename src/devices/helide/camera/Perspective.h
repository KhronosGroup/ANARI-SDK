// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Camera.h"
// std
#include <optional>

namespace helide {

struct Perspective : public Camera
{
  Perspective(HelideGlobalState *s);

  void commitParameters() override;

  RayGenerator createRayGenerator(float frameAspect) const override;

 private:
  float m_fovy{0.f};
  std::optional<float> m_aspect;
};

} // namespace helide
