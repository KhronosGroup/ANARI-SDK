// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Spheres.h"

namespace anari {
namespace example_device {

// Sphere primitive definitions ///////////////////////////////////////////////

Sphere::Sphere(const vec3 &origin, float radius)
    : m_origin(origin), m_radius(radius)
{}

box3 Sphere::bounds() const
{
  return box3(m_origin - vec3(m_radius), m_origin + vec3(m_radius));
}

PotentialHit Sphere::intersect(const Ray &ray, size_t primID) const
{
  auto oc = ray.org - m_origin;
  auto a = dot(ray.dir, ray.dir);
  auto b = 2 * dot(ray.dir, oc);
  auto c = dot(oc, oc) - m_radius * m_radius;

  auto delta = b * b - 4 * a * c;
  if (delta >= 0) {
    auto inv = float(0.5) / a;
    auto t0 = -(b + std::sqrt(delta)) * inv;
    auto t1 = -(b - std::sqrt(delta)) * inv;
    auto t = std::fmin(t0 > ray.t.lower ? t0 : t1, t1 > ray.t.lower ? t1 : t0);
    if (t > ray.t.lower && t < ray.t.upper) {
      GeometryInfo info;
      info.normal = (ray.org + t * ray.dir) - m_origin;
      return makeHit(t, primID, info);
    }
  }

  return std::nullopt;
}

// Spheres geometry definitions ///////////////////////////////////////////////

void Spheres::commit()
{
  Geometry::commit();

  m_positionData = getParamObject<Array1D>("vertex.position");
  m_radiusData = getParamObject<Array1D>("vertex.radius");
  m_colorArray = getParamObject<Array1D>("vertex.color");
  m_color = m_colorArray ? m_colorArray->dataAs<vec4>() : m_color;

  if (!m_positionData)
    throw std::runtime_error("missing 'vertex.position' on sphere geometry");

  float globalRadius = getParam<float>("radius", 0.01f);

  m_spheres.clear();
  m_spheres.reserve(m_positionData->size());

  auto *begin = m_positionData->dataAs<vec3>();
  auto *end = begin + m_positionData->size();

  float *radius = nullptr;
  if (m_radiusData)
    radius = m_radiusData->dataAs<float>();

  size_t sphereID = 0;
  std::transform(begin, end, std::back_inserter(m_spheres), [&](const vec3 &v) {
    return Sphere(v, radius ? radius[sphereID++] : globalRadius);
  });

  buildBVH(m_spheres.data(), m_spheres.size());
}

} // namespace example_device
} // namespace anari
