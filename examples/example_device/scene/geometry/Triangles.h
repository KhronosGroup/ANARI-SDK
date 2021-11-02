// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"

namespace anari {
namespace example_device {

struct Triangle : public Primitive
{
  Triangle(const vec3 *vertices, uvec3 i);

  box3 bounds() const override;

  PotentialHit intersect(const Ray &ray, size_t primID) const override;

 private:
  vec3 m_p0, m_e1, m_e2, m_n;
  uvec3 m_idx;
};

struct Triangles : public IntersectableObject<Triangle, Geometry>
{
  Triangles() = default;

  void commit() override;

 private:
  friend struct Triangle;

  std::vector<Triangle> m_triangles;

  IntrusivePtr<Array1D> m_vertices;
  IntrusivePtr<Array1D> m_indices;
  IntrusivePtr<Array1D> m_colorData;

  vec4 *m_colors{nullptr};
};

} // namespace example_device
} // namespace anari
