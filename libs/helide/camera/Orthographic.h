// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Camera.h"

namespace helide {

struct Orthographic : public Camera
{
  Orthographic(HelideGlobalState *s);

  void commit() override;

  Ray createRay(const float2 &screen) const override;

 private:
   float3 m_pos_du;
   float3 m_pos_dv;
   float3 m_pos_00;
};

} // namespace helide
