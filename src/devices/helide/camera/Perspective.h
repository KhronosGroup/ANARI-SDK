// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Camera.h"

namespace helide {

struct Perspective : public Camera
{
  Perspective(HelideGlobalState *s);

  void commitParameters() override;
  void finalize() override;

  Ray createRay(const float2 &screen) const override;

 private:
   float m_fovy{0.f};
   float m_aspect{1.f};
   float3 m_dir_du;
   float3 m_dir_dv;
   float3 m_dir_00;
};

} // namespace helide
