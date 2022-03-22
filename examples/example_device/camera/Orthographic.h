// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Camera.h"

namespace anari {
namespace example_device {

struct Orthographic : public Camera
{
  Orthographic() = default;

  static ANARIParameter g_parameters[];

  void commit() override;

  Ray createRay(const vec2 &screen) const override;

 private:
   vec3 m_pos_du;
   vec3 m_pos_dv;
   vec3 m_pos_00;
};

} // namespace example_device
} // namespace anari
