// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Debug.h"

namespace anari {
namespace example_device {

RenderedSample Debug::renderSample(Ray ray, const World &world) const
{
  if (auto hit = closestHit(ray, world); hitGeometry(hit))
    return {vec4(makeRandomColor(hit->instID.value()), 1.f), hit->t.lower};
  else
    return m_bgColor;
}

} // namespace example_device
} // namespace anari
