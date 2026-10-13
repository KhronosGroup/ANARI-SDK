// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Camera.h"

namespace anari_hydra {

struct Perspective : public Camera
{
  Perspective(HydraGlobalState *s);
  ~Perspective() override = default;

  void commitParameters() override;

 private:
  double imageHeight() const override;
  float m_fovy{1.0471975512f}; // vertical field of view, radians
};

} // namespace anari_hydra
