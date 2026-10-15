// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Sphere.h"

#include <pxr/imaging/hd/tokens.h>
#include <cmath>

PXR_NAMESPACE_USING_DIRECTIVE

namespace anari_hydra {

Sphere::Sphere(HydraGlobalState *s) : Geometry(s) {}

void Sphere::finalize()
{
  // Array unmaps enqueue finalize without re-committing parameters.
  Geometry::finalize();
  m_mesh = TessMesh{};
  auto *positions = vertexPosition();
  if (!positions)
    return;
  const auto *centers = static_cast<const GfVec3f *>(positions->data());
  const auto *indices32 = index() && index()->elementType() == ANARI_UINT32
      ? static_cast<const uint32_t *>(index()->data())
      : nullptr;
  const auto *indices64 = index() && index()->elementType() == ANARI_UINT64
      ? static_cast<const uint64_t *>(index()->data())
      : nullptr;
  const auto *radii = vertexRadius()
      ? static_cast<const float *>(vertexRadius()->data())
      : nullptr;
  const size_t count = index() ? index()->size() : positions->size();
  // 32 longitudes, 16 latitude intervals: 482 vertices, 960 triangles.
  // Inscribed surface radial error <= 1-cos(pi/16), or 1.93% of radius.
  constexpr int longitude = 32;
  constexpr int latitude = 16;
  constexpr float pi = 3.14159265358979323846f;
  for (size_t primitive = 0; primitive < count; ++primitive) {
    const size_t vertex = indices64 ? indices64[primitive]
        : indices32                 ? indices32[primitive]
                                    : primitive;
    if (vertex >= positions->size()
        || (radii && vertex >= vertexRadius()->size()))
      continue;
    const float radius = radii ? radii[vertex] : globalRadius();
    if (!(radius > 0.f) || !std::isfinite(radius))
      continue;
    const int base = int(m_mesh.points.size());
    auto point = [&](const GfVec3f &normal) {
      m_mesh.points.push_back(centers[vertex] + radius * normal);
      m_mesh.normals.push_back(normal);
      m_mesh.vertexIndices.push_back(uint32_t(vertex));
    };
    point(GfVec3f(0, 0, 1));
    for (int j = 1; j < latitude; ++j) {
      const float theta = pi * j / latitude;
      for (int k = 0; k < longitude; ++k) {
        const float phi = 2.f * pi * k / longitude;
        point(GfVec3f(std::sin(theta) * std::cos(phi),
            std::sin(theta) * std::sin(phi),
            std::cos(theta)));
      }
    }
    point(GfVec3f(0, 0, -1));
    auto triangle = [&](int a, int b, int c) {
      m_mesh.faceVertexCounts.push_back(3);
      m_mesh.faceVertexIndices.push_back(base + a);
      m_mesh.faceVertexIndices.push_back(base + b);
      m_mesh.faceVertexIndices.push_back(base + c);
      m_mesh.primitiveIndices.push_back(uint32_t(primitive));
    };
    for (int k = 0; k < longitude; ++k) {
      const int next = (k + 1) % longitude;
      triangle(0, 1 + k, 1 + next);
      for (int j = 0; j < latitude - 2; ++j) {
        const int a = 1 + j * longitude + k;
        const int b = 1 + j * longitude + next;
        triangle(a, a + longitude, b);
        triangle(b, a + longitude, b + longitude);
      }
      const int ring = 1 + (latitude - 2) * longitude;
      triangle(ring + k, 1 + (latitude - 1) * longitude, ring + next);
    }
  }
}

TfToken Sphere::primType() const
{
  return HdPrimTypeTokens->mesh;
}

} // namespace anari_hydra
