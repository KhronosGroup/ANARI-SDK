// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "RayDir.h"

namespace anari {
namespace example_device {

RenderedSample RayDir::renderSample(Ray ray, const World &) const
{
  return vec4(ray.dir, 1.f);
}

} // namespace example_device
} // namespace anari
