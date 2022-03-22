// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"

namespace anari {
namespace example_device {

struct Sphere : public Primitive
{
  Sphere(const vec3 &origin, float radius);

  box3 bounds() const override;

  PotentialHit intersect(const Ray &ray, size_t primID) const override;

 private:
  vec3 m_origin;
  float m_radius;
};

struct Spheres : public IntersectableObject<Sphere, Geometry>
{
  Spheres() = default;

  static ANARIParameter g_parameters[];

  void commit() override;

 private:
  IntrusivePtr<Array1D> m_positionData;
  IntrusivePtr<Array1D> m_radiusData;
  IntrusivePtr<Array1D> m_sphereColors;

  std::vector<Sphere> m_spheres;
};

} // namespace example_device
} // namespace anari
