// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "TubeMesh.h"

#include <cmath>

PXR_NAMESPACE_USING_DIRECTIVE

namespace anari_hydra {

// Radial resolution of the swept tube. Fixed; perf tuning is out of v1 scope.
static constexpr int kRadial = 24;

static GfVec3f Normalize(const GfVec3f &v)
{
  const float len = v.GetLength();
  return len > 1e-8f ? v / len : GfVec3f(0, 0, 1);
}

// A stable frame perpendicular to `axis`.
static void PerpFrame(const GfVec3f &axis, GfVec3f &u, GfVec3f &v)
{
  const GfVec3f ref =
      std::fabs(axis[0]) < 0.9f ? GfVec3f(1, 0, 0) : GfVec3f(0, 1, 0);
  u = Normalize(GfCross(ref, axis));
  v = GfCross(axis, u);
}

TubeCaps ParseTubeCaps(const std::string &caps)
{
  return {
      caps == "first" || caps == "both", caps == "second" || caps == "both"};
}

void TessellateTubes(helium::Array1D *position,
    helium::Array1D *index,
    const TubeRadii &radii,
    helium::Array1D *vertexCap,
    TubeCaps caps,
    TessMesh &out)
{
  out.primitiveIndices.clear();
  out.vertexIndices.clear();
  out.points.clear();
  out.faceVertexCounts.clear();
  out.faceVertexIndices.clear();
  if (!position || position->size() < 2)
    return;

  const GfVec3f *pos = static_cast<const GfVec3f *>(position->data());
  const float *vertexRadii =
      radii.vertex ? static_cast<const float *>(radii.vertex->data()) : nullptr;
  const float *primRadii = radii.primitive
      ? static_cast<const float *>(radii.primitive->data())
      : nullptr;
  const uint8_t *vcaps =
      vertexCap ? static_cast<const uint8_t *>(vertexCap->data()) : nullptr;

  // Segment endpoint index pairs: explicit primitive.index (UINT32_VEC2) or
  // consecutive vertex pairs.
  std::vector<std::pair<uint32_t, uint32_t>> segs;
  if (index && index->size() > 0) {
    const uint32_t *idx = static_cast<const uint32_t *>(index->data());
    for (size_t i = 0; i < index->size(); ++i)
      segs.emplace_back(idx[2 * i], idx[2 * i + 1]);
  } else {
    for (size_t i = 0; i + 1 < position->size(); i += 2)
      segs.emplace_back(uint32_t(i), uint32_t(i + 1));
  }

  for (size_t primitive = 0; primitive < segs.size(); ++primitive) {
    const auto &seg = segs[primitive];
    if (seg.first >= position->size() || seg.second >= position->size())
      continue;
    const GfVec3f p0 = pos[seg.first];
    const GfVec3f p1 = pos[seg.second];
    float r0, r1;
    if (primRadii && primitive < radii.primitive->size()) {
      r0 = r1 = primRadii[primitive];
    } else if (vertexRadii && seg.first < radii.vertex->size()
        && seg.second < radii.vertex->size()) {
      r0 = vertexRadii[seg.first];
      r1 = vertexRadii[seg.second];
    } else {
      r0 = radii.base;
      r1 = radii.apex;
    }

    GfVec3f u, v;
    PerpFrame(Normalize(p1 - p0), u, v);

    const int base = int(out.points.size());
    for (int k = 0; k < kRadial; ++k) {
      const float a = 2.f * float(M_PI) * float(k) / float(kRadial);
      const GfVec3f dir = std::cos(a) * u + std::sin(a) * v;
      out.vertexIndices.push_back(seg.first);
      out.vertexIndices.push_back(seg.second);
      out.points.push_back(p0 + r0 * dir); // ring 0: base + 2k
      out.points.push_back(p1 + r1 * dir); // ring 1: base + 2k + 1
    }
    for (int k = 0; k < kRadial; ++k) {
      const int k1 = (k + 1) % kRadial;
      out.primitiveIndices.push_back(uint32_t(primitive));
      out.faceVertexCounts.push_back(4);
      out.faceVertexIndices.push_back(base + 2 * k); // ring0 k
      out.faceVertexIndices.push_back(base + 2 * k1); // ring0 k+1
      out.faceVertexIndices.push_back(base + 2 * k1 + 1); // ring1 k+1
      out.faceVertexIndices.push_back(base + 2 * k + 1); // ring1 k
    }

    // End caps: a triangle fan around the ring center, only where the end has
    // area (a cone's zero apex needs none). The p0 fan is wound opposite the
    // p1 fan so both normals face outward along the axis — Storm's doubleSided
    // shading forgives an inward cap, but Embree's facing-based shading
    // renders it dark.
    auto emitCap = [&](const GfVec3f &center, int ringStart, bool flip) {
      const int c = int(out.points.size());
      out.points.push_back(center);
      out.vertexIndices.push_back(flip ? seg.first : seg.second);
      for (int k = 0; k < kRadial; ++k) {
        const int k1 = (k + 1) % kRadial;
        out.primitiveIndices.push_back(uint32_t(primitive));
        out.faceVertexCounts.push_back(3);
        out.faceVertexIndices.push_back(c);
        out.faceVertexIndices.push_back(ringStart + 2 * (flip ? k1 : k));
        out.faceVertexIndices.push_back(ringStart + 2 * (flip ? k : k1));
      }
    };
    const bool cap0 = vcaps && seg.first < vertexCap->size()
        ? vcaps[seg.first] != 0
        : caps.first;
    const bool cap1 = vcaps && seg.second < vertexCap->size()
        ? vcaps[seg.second] != 0
        : caps.second;
    if (cap0 && r0 > 0.f)
      emitCap(p0, base, true); // ring 0 vertices sit at base + 2k
    if (cap1 && r1 > 0.f)
      emitCap(p1, base + 1, false); // ring 1 vertices sit at base + 2k + 1
  }
}

} // namespace anari_hydra
