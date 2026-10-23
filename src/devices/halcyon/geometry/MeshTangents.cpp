// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: LicenseRef-TOST-1.0
//
// smoothTangentFrames() and MeshTangentInput::faceNormal() are ported from
// Typhoon (NVIDIA-Omniverse/OpenUSD, branch typhoon/main, commit
// 70c45e83b08a7b45588cd6cdbd2ae67f5147befb), pxr/imaging/plugin/hdEmbree/
// delegate/mesh.cpp (HdEmbreeMesh::_UpdateTangentFrameCache(),
// _BuildGeometricFaceNormal(), _MakeSmoothingKey()), and the vector helpers
// from OpenUSD's GfVec3f (pxr/base/gf/vec3f.h: Normalize(), GfCross()) and
// GfBuildOrthonormalFrame() (pxr/base/gf/vec3f.cpp). Copyright 2016-2026
// Pixar, licensed under the Tomorrow Open Source Technology License 1.0 (see
// THIRD-PARTY.md). Keep in step with those files when the pin in
// external/typhoon/PROVENANCE.md moves.

#include "MeshTangents.h"
// std
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace halcyon {

using float3 = anari::math::float3;
using float4 = anari::math::float4;

namespace {

// GfVec3f arithmetic, operation for operation, so that frames match
// Typhoon's bit for bit

float dot(const float3 &a, const float3 &b)
{
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

float lengthSq(const float3 &v)
{
  return dot(v, v);
}

float3 cross(const float3 &a, const float3 &b)
{
  return float3(
      a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

// GfVec3f's operator*=(double): each component times 's' in double
float3 scaled(const float3 &v, double s)
{
  return float3(float(v.x * s), float(v.y * s), float(v.z * s));
}

// GF_MIN_VECTOR_LENGTH
constexpr float MIN_VECTOR_LENGTH = 1e-10f;

// GfVec3f::Normalize(): times 1 / length, at least MIN_VECTOR_LENGTH
void normalize(float3 &v)
{
  const float length = std::sqrt(lengthSq(v));
  v = scaled(
      v, 1.0 / (length > MIN_VECTOR_LENGTH ? length : MIN_VECTOR_LENGTH));
}

// GfBuildOrthonormalFrame(): two unit vectors perpendicular to 'v0' and each
// other, v1 x v2 along v0, scaled down when |v0| < MIN_VECTOR_LENGTH
void buildOrthonormalFrame(const float3 &v0, float3 &v1, float3 &v2)
{
  const float len = std::sqrt(lengthSq(v0));
  if (len == 0.) {
    v1 = v2 = float3(0.f);
    return;
  }
  const float3 unitDir = scaled(v0, 1.0 / len);
  v1 = cross(float3(1.f, 0.f, 0.f), unitDir);
  if (lengthSq(v1) < 1e-4 * 1e-4)
    v1 = cross(float3(0.f, 1.f, 0.f), unitDir);
  normalize(v1);
  v2 = cross(unitDir, v1);
  if (len < MIN_VECTOR_LENGTH) {
    const double desiredLen = len / MIN_VECTOR_LENGTH;
    v1 = scaled(v1, desiredLen);
    v2 = scaled(v2, desiredLen);
  }
}

// Typhoon's _BuildGeometricFaceNormal()
float3 buildGeometricFaceNormal(
    const float3 &p0, const float3 &p1, const float3 &p2)
{
  float3 normal = cross(p1 - p0, p2 - p0);
  if (lengthSq(normal) > 1e-18f) {
    normalize(normal);
    return normal;
  }
  float3 fallback0, fallback1;
  buildOrthonormalFrame(float3(0.f, 0.f, 1.f), fallback0, fallback1);
  float3 n = cross(fallback0, fallback1);
  normalize(n);
  return n;
}

// Typhoon's smoothing key, without the vertex: a normal's exact bits
struct NormalBits
{
  uint32_t bits[3];

  explicit NormalBits(const float3 &n)
  {
    std::memcpy(&bits[0], &n.x, sizeof(float));
    std::memcpy(&bits[1], &n.y, sizeof(float));
    std::memcpy(&bits[2], &n.z, sizeof(float));
  }

  bool operator==(const NormalBits &o) const
  {
    return bits[0] == o.bits[0] && bits[1] == o.bits[1] && bits[2] == o.bits[2];
  }
};

float4 packed(const float3 &v)
{
  return float4(v.x, v.y, v.z, 0.f);
}

} // namespace

// MeshTangentInput ///////////////////////////////////////////////////////////

size_t MeshTangentInput::numCorners() const
{
  return numPrimitives * cornersPerPrimitive;
}

uint32_t MeshTangentInput::vertex(size_t corner) const
{
  const size_t v = indices ? indices[corner] : corner;
  return uint32_t(std::min(v, numPositions - 1));
}

float3 MeshTangentInput::faceNormal(size_t corner) const
{
  const size_t first = corner - corner % cornersPerPrimitive;
  const size_t i = corner - first;
  if (cornersPerPrimitive == 3) {
    return buildGeometricFaceNormal(positions[vertex(first)],
        positions[vertex(first + 1)],
        positions[vertex(first + 2)]);
  }
  // a quad's corner, the next and the previous, so that a planar quad's
  // corners agree with its winding
  const size_t n = cornersPerPrimitive;
  return buildGeometricFaceNormal(positions[vertex(corner)],
      positions[vertex(first + (i + 1) % n)],
      positions[vertex(first + (i + n - 1) % n)]);
}

float3 MeshTangentInput::cornerNormal(size_t corner) const
{
  float3 normal;
  if (numFaceVaryingNormals > 0) {
    const float4 &n =
        faceVaryingNormals[std::min(corner, numFaceVaryingNormals - 1)];
    normal = float3(n.x, n.y, n.z);
  } else if (numVertexNormals > 0) {
    const float4 &n =
        vertexNormals[std::min<size_t>(vertex(corner), numVertexNormals - 1)];
    normal = float3(n.x, n.y, n.z);
  } else {
    normal = faceNormal(corner);
  }
  // Typhoon: the face's normal where the corner's is degenerate
  if (lengthSq(normal) < 1e-18f)
    return faceNormal(corner);
  normalize(normal);
  return normal;
}

// Frames /////////////////////////////////////////////////////////////////////

void serialFor(size_t n, const std::function<void(size_t, size_t)> &f)
{
  f(0, n);
}

// A counting sort by vertex: each vertex's corners counted and scattered
// (concurrently, so in any order), then sorted back into corner order
MeshCornersByVertex::MeshCornersByVertex(
    const MeshTangentInput &mesh, const ParallelFor &parallelFor)
{
  const size_t numCorners = mesh.numCorners();
  const size_t numVertices = mesh.numPositions;
  if (numCorners == 0 || numVertices == 0)
    return;

  std::unique_ptr<std::atomic<size_t>[]> next(
      new std::atomic<size_t>[numVertices]);
  parallelFor(numVertices, [&](size_t begin, size_t end) {
    for (size_t v = begin; v < end; v++)
      next[v].store(0, std::memory_order_relaxed);
  });
  parallelFor(numCorners, [&](size_t begin, size_t end) {
    for (size_t c = begin; c < end; c++)
      next[mesh.vertex(c)].fetch_add(1, std::memory_order_relaxed);
  });

  start.resize(numVertices + 1);
  start[0] = 0;
  for (size_t v = 0; v < numVertices; v++) {
    start[v + 1] = start[v] + next[v].load(std::memory_order_relaxed);
    next[v].store(start[v], std::memory_order_relaxed);
  }

  corners.resize(numCorners);
  parallelFor(numCorners, [&](size_t begin, size_t end) {
    for (size_t c = begin; c < end; c++) {
      corners[next[mesh.vertex(c)].fetch_add(1, std::memory_order_relaxed)] = c;
    }
  });
  parallelFor(numVertices, [&](size_t begin, size_t end) {
    for (size_t v = begin; v < end; v++)
      std::sort(corners.begin() + start[v], corners.begin() + start[v + 1]);
  });
}

size_t MeshCornersByVertex::numVertices() const
{
  return start.empty() ? 0 : start.size() - 1;
}

// Typhoon groups corners by vertex and exact normal in a hash map, each group
// accumulating its corners in corner order, then orthonormalizes each
// corner's frame. A group's corners share a vertex, so here each vertex's
// corners (in corner order, MeshCornersByVertex) are grouped by their
// normals, accumulated and orthonormalized on their own, which does the same
// operations in the same order.
void smoothTangentFrames(const MeshTangentInput &mesh,
    const float3 *dPdu,
    const float3 *dPdv,
    const MeshCornersByVertex &byVertex,
    size_t vertexBegin,
    size_t vertexEnd,
    float4 *tangents,
    float4 *bitangents)
{
  struct Group
  {
    NormalBits key;
    float3 tangent{0.f};
    float3 bitangent{0.f};
  };
  std::vector<Group> groups;
  std::vector<size_t> cornerGroups;
  std::vector<float3> cornerNormals;

  for (size_t v = vertexBegin; v < vertexEnd; v++) {
    const size_t first = byVertex.start[v];
    const size_t count = byVertex.start[v + 1] - first;
    groups.clear();
    cornerGroups.resize(count);
    cornerNormals.resize(count);

    for (size_t k = 0; k < count; k++) {
      const size_t corner = byVertex.corners[first + k];
      const float3 &du = dPdu[corner];
      const float3 &dv = dPdv[corner];

      const float3 normal = mesh.cornerNormal(corner);
      cornerNormals[k] = normal;

      float3 rawTangent = du - scaled(normal, dot(normal, du));
      if (lengthSq(rawTangent) < 1e-18f)
        rawTangent = cross(dv, normal);

      if (lengthSq(rawTangent) < 1e-18f) {
        float3 fallbackBitangent;
        buildOrthonormalFrame(normal, rawTangent, fallbackBitangent);
      } else {
        normalize(rawTangent);
      }

      float3 rawBitangent = cross(normal, rawTangent);
      if (lengthSq(rawBitangent) < 1e-18f) {
        float3 fallbackTangent;
        buildOrthonormalFrame(normal, fallbackTangent, rawBitangent);
        rawTangent = fallbackTangent;
      } else {
        normalize(rawBitangent);
        if (dot(rawBitangent, dv) < 0.0f)
          rawBitangent = scaled(rawBitangent, -1.0f);
      }

      const NormalBits key(normal);
      size_t g = 0;
      while (g < groups.size() && !(groups[g].key == key))
        g++;
      if (g == groups.size())
        groups.push_back({key});
      cornerGroups[k] = g;

      Group &accum = groups[g];
      if (lengthSq(accum.tangent) > 1e-18f
          && dot(rawTangent, accum.tangent) < 0.0f) {
        rawTangent = scaled(rawTangent, -1.0f);
        rawBitangent = scaled(rawBitangent, -1.0f);
      }
      accum.tangent += rawTangent;
      accum.bitangent += rawBitangent;
    }

    for (size_t k = 0; k < count; k++) {
      const size_t corner = byVertex.corners[first + k];
      const Group &accum = groups[cornerGroups[k]];
      float3 normal = cornerNormals[k];
      if (lengthSq(normal) < 1e-18f)
        normal = float3(0.f, 1.f, 0.f);
      else
        normalize(normal);

      float3 tangent =
          accum.tangent - scaled(normal, dot(normal, accum.tangent));
      if (lengthSq(tangent) < 1e-18f)
        tangent = cross(accum.bitangent, normal);
      if (lengthSq(tangent) < 1e-18f) {
        float3 fallbackBitangent;
        buildOrthonormalFrame(normal, tangent, fallbackBitangent);
        tangents[corner] = packed(tangent);
        bitangents[corner] = packed(fallbackBitangent);
        continue;
      }

      normalize(tangent);

      float3 bitangent =
          accum.bitangent - scaled(normal, dot(normal, accum.bitangent));
      bitangent = bitangent - scaled(tangent, dot(tangent, bitangent));
      if (lengthSq(bitangent) < 1e-18f)
        bitangent = cross(normal, tangent);
      if (lengthSq(bitangent) < 1e-18f) {
        float3 fallbackTangent, fallbackBitangent;
        buildOrthonormalFrame(normal, fallbackTangent, fallbackBitangent);
        tangents[corner] = packed(fallbackTangent);
        bitangents[corner] = packed(fallbackBitangent);
        continue;
      }

      normalize(bitangent);

      tangents[corner] = packed(tangent);
      bitangents[corner] = packed(bitangent);
    }
  }
}

void smoothTangentFrames(const MeshTangentInput &mesh,
    const float3 *dPdu,
    const float3 *dPdv,
    std::vector<float4> &tangents,
    std::vector<float4> &bitangents)
{
  const MeshCornersByVertex byVertex(mesh);
  tangents.resize(mesh.numCorners());
  bitangents.resize(mesh.numCorners());
  smoothTangentFrames(mesh,
      dPdu,
      dPdv,
      byVertex,
      0,
      byVertex.numVertices(),
      tangents.data(),
      bitangents.data());
}

void authoredTangentFrames(const MeshTangentInput &mesh,
    const float4 *values,
    size_t numValues,
    bool faceVarying,
    size_t cornerBegin,
    size_t cornerEnd,
    float4 *tangents,
    float4 *bitangents)
{
  for (size_t corner = cornerBegin; corner < cornerEnd; corner++) {
    const size_t e = faceVarying ? corner : mesh.vertex(corner);
    const float4 &value = values[std::min(e, numValues - 1)];
    const float3 tangent(value.x, value.y, value.z);
    const float handedness = value.w < 0.f ? -1.f : 1.f;
    tangents[corner] = packed(tangent);
    bitangents[corner] =
        packed(handedness * cross(mesh.cornerNormal(corner), tangent));
  }
}

} // namespace halcyon
