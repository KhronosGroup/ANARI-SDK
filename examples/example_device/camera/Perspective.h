// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Camera.h"

namespace anari {
namespace example_device {

struct Perspective : public Camera
{
  Perspective() = default;

  static ANARIParameter g_parameters[];

  void commit() override;

  Ray createRay(const vec2 &screen) const override;

 private:
   vec3 m_dir_du;
   vec3 m_dir_dv;
   vec3 m_dir_00;
};

} // namespace example_device
} // namespace anari
