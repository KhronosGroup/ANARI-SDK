// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include "anari/anari_cpp.hpp"
#include "anari/anari_cpp/ext/linalg.h"
// std
#include <cstdint>
#include <string_view>
#include <vector>

namespace anari {
namespace cts {

using uint2 = math::vec<uint32_t, 2>;
using uint3 = math::vec<uint32_t, 3>;
using uint4 = math::vec<uint32_t, 4>;

enum class PrimitiveMode
{
  Soup,
  Indexed,
};

enum class TriangleShape
{
  Triangle,
  Quad,
  Cube,
};

enum class QuadShape
{
  Quad,
  Cube,
};

struct TriangleLayout
{
  std::vector<math::float3> positions;
  std::vector<uint3> indices;
};

struct QuadLayout
{
  std::vector<math::float3> positions;
  std::vector<uint4> indices;
};

struct SphereLayout
{
  std::vector<math::float3> positions;
  std::vector<uint32_t> indices;
};

struct CurveLayout
{
  std::vector<math::float3> positions;
  std::vector<uint32_t> indices;
};

struct SegmentLayout
{
  std::vector<math::float3> positions;
  std::vector<uint2> indices;
};

PrimitiveMode parsePrimitiveMode(
    std::string_view mode);

TriangleLayout makeTriangleLayout(
    TriangleShape shape,
    PrimitiveMode mode,
    int primitiveCount,
    bool unusedVertices = false);

QuadLayout makeQuadLayout(QuadShape shape,
    PrimitiveMode mode,
    int primitiveCount,
    bool unusedVertices = false);

SphereLayout makeSphereLayout(
    PrimitiveMode mode, int primitiveCount, bool unusedVertices = false);

CurveLayout makeCurveLayout(
    PrimitiveMode mode, int primitiveCount, bool unusedVertices = false);

SegmentLayout makeSegmentLayout(
    PrimitiveMode mode, int primitiveCount, bool unusedVertices = false);

// The canonical equilateral-triangle Layout: three vertices per triangle in
// soup order. Material tests use it to add their own attribute arrays without
// depending on ANARI object publication.
std::vector<math::float3> layoutTriangleSoup(
    int primitiveCount);

} // namespace cts
} // namespace anari
