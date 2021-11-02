// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Material.h"

namespace anari {
namespace example_device {

struct OBJ : public Material
{
  OBJ() = default;

  void commit() override;

  vec3 diffuse() const override;

 private:
   vec3 m_Kd{1.f};
};

} // namespace example_device
} // namespace anari
