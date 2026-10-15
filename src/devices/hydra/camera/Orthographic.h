// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "Camera.h"
namespace anari_hydra {
struct Orthographic : public Camera
{
  Orthographic(HydraGlobalState *s) : Camera(s) {}
  void commitParameters() override;
  bool isOrthographic() const override
  {
    return true;
  }

 private:
  double imageHeight() const override
  {
    return m_height;
  }
  float m_height{1.f};
};
} // namespace anari_hydra
