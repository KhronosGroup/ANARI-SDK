// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Raycast.h"

namespace anari {
namespace example_device {

RenderedSample Raycast::renderSample(Ray ray, const World &world) const
{
  if (auto hit = closestHit(ray, world); hitGeometry(hit)) {
    GeometryInfo &info = getGeometryInfo(hit);

    vec3 color(info.color.x, info.color.y, info.color.z);

    if (info.material)
      color *= info.material->diffuse();

    return {vec4(dot(info.normal, -ray.dir) * color, 1.f), hit->t.lower};
  } else
    return m_bgColor;
}

} // namespace example_device
} // namespace anari
