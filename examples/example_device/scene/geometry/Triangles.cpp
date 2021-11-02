// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Triangles.h"

#include "Surface.h"

namespace anari {
namespace example_device {

// Triangle primitive definitions /////////////////////////////////////////////

Triangle::Triangle(const vec3 *vertices, uvec3 idx) : m_idx(idx)
{
  const vec3 p0 = vertices[idx.x];
  const vec3 p1 = vertices[idx.y];
  const vec3 p2 = vertices[idx.z];

  m_p0 = p0;
  m_e1 = p0 - p1;
  m_e2 = (p2 - p0);

  m_n = cross(m_e1, m_e2);
}

box3 Triangle::bounds() const
{
  box3 bbox(m_p0);
  bbox.extend(m_p0 - m_e1);
  bbox.extend(m_p0 + m_e2);
  return bbox;
}

PotentialHit Triangle::intersect(const Ray &ray, size_t primID) const
{
  auto c = m_p0 - ray.org;
  auto r = cross(ray.dir, c);
  auto inv_det = 1.f / dot(m_n, ray.dir);

  auto u = dot(r, m_e2) * inv_det;
  auto v = dot(r, m_e1) * inv_det;
  auto w = 1.f - u - v;

  if (u >= 0 && v >= 0 && w >= 0) {
    auto t = dot(m_n, c) * inv_det;
    if (t >= ray.t.lower && t < ray.t.upper) {
      GeometryInfo info;
      info.u = u;
      info.v = v;
      info.normal = m_n;

      auto *model = (const Surface *)ray.currentSurface;
      auto *geometry = (const Triangles *)model->geometry();
      auto *colors = geometry->m_colors;
      if (colors) {
        info.color =
            w * colors[m_idx.x] + u * colors[m_idx.y] + v * colors[m_idx.z];
      }

      return makeHit(t, primID, info);
    }
  }

  return std::nullopt;
}

// Triangles geometry definitions /////////////////////////////////////////////

void Triangles::commit()
{
  Geometry::commit();

  m_indices = getParamObject<Array1D>(
      "primitive.index", getParamObject<Array1D>("index"));
  m_vertices = getParamObject<Array1D>("vertex.position");
  m_colorData = getParamObject<Array1D>("vertex.color");

  m_colors = m_colorData ? m_colorData->dataAs<vec4>() : nullptr;

  m_triangles.clear();

  auto *vertices = m_vertices->dataAs<vec3>();

  if (m_indices) {
    m_triangles.reserve(m_indices->size());

    auto *begin = m_indices->dataAs<uvec3>();
    auto *end = begin + m_indices->size();

    std::transform(
        begin, end, std::back_inserter(m_triangles), [&](const uvec3 &i) {
          return Triangle(vertices, i);
        });
  } else {
    m_triangles.reserve(m_vertices->size() / 3);

    for (uint32_t i = 0; i < m_vertices->size(); i += 3)
      m_triangles.emplace_back(vertices, uvec3(0, 1, 2) + i);
  }

  buildBVH(m_triangles.data(), m_triangles.size());
}

} // namespace example_device
} // namespace anari
