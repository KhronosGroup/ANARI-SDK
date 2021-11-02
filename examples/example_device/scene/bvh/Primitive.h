// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../../glm_extras.h"

namespace anari {
namespace example_device {

struct Primitive
{
  virtual box3 bounds() const = 0;
  virtual PotentialHit intersect(const Ray &ray, size_t primID) const = 0;
};

} // namespace example_device
} // namespace anari