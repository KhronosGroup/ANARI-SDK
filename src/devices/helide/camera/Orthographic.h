// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Camera.h"
// std
#include <optional>

namespace helide {

struct Orthographic : public Camera
{
  Orthographic(HelideGlobalState *s);

  void commitParameters() override;

  RayGenerator createRayGenerator(float frameAspect) const override;

 private:
  std::optional<float> m_aspect;
  float m_height{1.f};
};

} // namespace helide
