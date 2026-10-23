// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// The tangent frames of triangle and quad meshes, one per primitive corner,
// which the pathtracer's shading context interpolates (renderer/pathtracer/
// ShadingContext.isph): authored tangents ('vertex.tangent',
// 'faceVarying.tangent') as given, else the frames Typhoon smooths across the
// faces that share a vertex and its normal (MeshTangents.cpp). Plain C++ over
// packed arrays.

// anari
#include <anari/anari_cpp/ext/linalg.h>
// std
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace halcyon {

// A triangle or quad mesh as its tangent frames read it. Its corners are
// numbered cornersPerPrimitive * primitive + i, like face-varying values.
struct MeshTangentInput
{
  using float3 = anari::math::float3;
  using float4 = anari::math::float4;

  // 3 (triangles) or 4 (quads)
  uint32_t cornersPerPrimitive{3};
  size_t numPrimitives{0};
  // cornersPerPrimitive vertex indices per primitive ('primitive.index'),
  // nullptr for implicit indexing (corner c is vertex c)
  const uint32_t *indices{nullptr};
  // 'vertex.position', at least one
  const float3 *positions{nullptr};
  size_t numPositions{0};
  // Shading normals as packed float4 (w unused), face-varying ones taking
  // precedence as in objectShadingNormal(); nullptr/0 if unset
  const float4 *faceVaryingNormals{nullptr};
  size_t numFaceVaryingNormals{0};
  const float4 *vertexNormals{nullptr};
  size_t numVertexNormals{0};

  size_t numCorners() const;
  // The vertex at 'corner', clamped into range like the ISPC hooks'
  uint32_t vertex(size_t corner) const;
  // The unit geometric normal of the face at 'corner' (Typhoon's
  // _BuildGeometricFaceNormal() of a triangle; for a quad, of the corner and
  // its two neighbours)
  float3 faceNormal(size_t corner) const;
  // The unit normal of the frame at 'corner': its shading normal (the
  // face-varying or vertex value, clamped into range), else, or where that
  // is degenerate, faceNormal(); normalized as Typhoon normalizes it
  float3 cornerNormal(size_t corner) const;
};

// Runs a function of a range [begin, end) over [0, n), in chunks that may
// run in parallel
using ParallelFor = std::function<void(
    size_t n, const std::function<void(size_t begin, size_t end)> &)>;

// A ParallelFor that runs serially
void serialFor(size_t n, const std::function<void(size_t, size_t)> &f);

// The corners of a mesh grouped by vertex, in corner order: those of vertex v
// are corners[start[v]] to corners[start[v + 1] - 1]
struct MeshCornersByVertex
{
  MeshCornersByVertex(
      const MeshTangentInput &mesh, const ParallelFor &parallelFor = serialFor);
  size_t numVertices() const;

  std::vector<size_t> start;
  std::vector<size_t> corners;
};

// Typhoon's smoothed tangent frames (HdEmbreeMesh::_UpdateTangentFrameCache())
// of the corners of vertices [vertexBegin, vertexEnd): for each corner, the
// surface derivatives 'dPdu'/'dPdv' at it (one per corner, from the
// geometry's derivative hook) give a unit tangent off the corner's normal and
// a bitangent, accumulated over the corners that share its vertex and exact
// normal, then orthonormalized again around the corner's normal. Writes one
// packed float4 (w 0) per corner to 'tangents' and 'bitangents' (one element
// per mesh corner). Vertex ranges are independent, so they may run in
// parallel.
void smoothTangentFrames(const MeshTangentInput &mesh,
    const anari::math::float3 *dPdu,
    const anari::math::float3 *dPdv,
    const MeshCornersByVertex &byVertex,
    size_t vertexBegin,
    size_t vertexEnd,
    anari::math::float4 *tangents,
    anari::math::float4 *bitangents);

// The same for the whole mesh, into vectors of one element per corner
void smoothTangentFrames(const MeshTangentInput &mesh,
    const anari::math::float3 *dPdu,
    const anari::math::float3 *dPdv,
    std::vector<anari::math::float4> &tangents,
    std::vector<anari::math::float4> &bitangents);

// Authored tangents 'values' (xyz the tangent, w its handedness, which only
// its sign counts) as the frames of corners [cornerBegin, cornerEnd): per
// corner the tangent as given and the bitangent sign(w) * N x tangent with N
// the corner's normal (cornerNormal()), as VisRTX and glTF build it.
// 'faceVarying' values are per corner, others per vertex; either clamps to
// its last element. Writes 'tangents' and 'bitangents' as above.
void authoredTangentFrames(const MeshTangentInput &mesh,
    const anari::math::float4 *values,
    size_t numValues,
    bool faceVarying,
    size_t cornerBegin,
    size_t cornerEnd,
    anari::math::float4 *tangents,
    anari::math::float4 *bitangents);

} // namespace halcyon
