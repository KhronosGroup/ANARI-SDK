// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Material.h"

namespace anari {
namespace example_device {

struct Matte : public Material
{
  Matte() = default;

  void commit() override;

  vec3 diffuse() const override;

 private:
   vec3 m_color{.8f,.8f,.8f};
};

} // namespace example_device
} // namespace anari
