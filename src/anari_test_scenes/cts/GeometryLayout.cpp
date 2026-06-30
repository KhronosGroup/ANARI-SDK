// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "GeometryLayout.h"
// std
#include <array>
#include <cmath>
#include <stdexcept>

namespace anari {
namespace cts {

namespace {

constexpr float kPitch = 0.5f;
constexpr float kGutter = 0.2f;
constexpr float kPrimSize = kPitch * (1.f - kGutter);
constexpr float kHalf = kPrimSize * 0.5f;
constexpr float kCubeYawDeg = 35.f;
constexpr float kCubePitchDeg = 25.f;

struct Grid
{
  int columns;
  int rows;
};

void validateSpecification(
    int primitiveCount, PrimitiveMode mode, bool unusedVertices)
{
  if (primitiveCount < 1)
    throw std::invalid_argument("geometry primitive count must be positive");
  if (unusedVertices && mode != PrimitiveMode::Indexed) {
    throw std::invalid_argument(
        "unused vertices require indexed primitive mode");
  }
  switch (mode) {
  case PrimitiveMode::Soup:
  case PrimitiveMode::Indexed:
    return;
  }
  throw std::invalid_argument("invalid geometry primitive mode enum value");
}

Grid gridFor(int primitiveCount)
{
  const int columns = static_cast<int>(
      std::ceil(std::sqrt(static_cast<double>(primitiveCount))));
  return {columns, (primitiveCount + columns - 1) / columns};
}

math::float3 cellCenter(int index, const Grid &grid)
{
  const int row = index / grid.columns;
  const int column = index % grid.columns;
  const float x =
      (static_cast<float>(column) - (grid.columns - 1) * 0.5f) * kPitch;
  const float y = ((grid.rows - 1) * 0.5f - static_cast<float>(row)) * kPitch;
  return math::float3(x, y, 0.f);
}

std::array<math::float3, 8> cubeCorners(const math::float3 &center)
{
  static const math::mat4 rotation = math::mul(
      math::rotation_matrix(math::rotation_quat(
          math::float3(1.f, 0.f, 0.f), anari::radians(kCubePitchDeg))),
      math::rotation_matrix(math::rotation_quat(
          math::float3(0.f, 1.f, 0.f), anari::radians(kCubeYawDeg))));
  static const std::array<math::float3, 8> unit = {math::float3(0, 0, 0),
      math::float3(1, 0, 0),
      math::float3(0, 1, 0),
      math::float3(0, 0, 1),
      math::float3(1, 1, 0),
      math::float3(1, 0, 1),
      math::float3(0, 1, 1),
      math::float3(1, 1, 1)};
  std::array<math::float3, 8> result;
  for (size_t i = 0; i < unit.size(); ++i) {
    const math::float3 local((unit[i].x * 2.f - 1.f) * kHalf,
        (unit[i].y * 2.f - 1.f) * kHalf,
        (unit[i].z * 2.f - 1.f) * kHalf);
    result[i] = center + math::mul(rotation, math::float4(local, 1.f)).xyz();
  }
  return result;
}

const std::array<uint3, 12> kCubeTriangles = {uint3{0, 2, 1},
    uint3{1, 2, 4},
    uint3{1, 4, 5},
    uint3{5, 4, 7},
    uint3{5, 7, 3},
    uint3{6, 7, 3},
    uint3{0, 3, 6},
    uint3{0, 6, 2},
    uint3{2, 7, 4},
    uint3{2, 6, 7},
    uint3{0, 5, 1},
    uint3{0, 3, 5}};

const std::array<uint4, 6> kCubeQuads = {uint4{0, 2, 4, 1},
    uint4{1, 4, 7, 5},
    uint4{5, 7, 6, 3},
    uint4{0, 3, 6, 2},
    uint4{2, 6, 7, 4},
    uint4{0, 1, 5, 3}};

std::array<math::float3, 4> squareCorners(const math::float3 &center)
{
  return {center + math::float3(-kHalf, -kHalf, 0.f),
      center + math::float3(kHalf, -kHalf, 0.f),
      center + math::float3(kHalf, kHalf, 0.f),
      center + math::float3(-kHalf, kHalf, 0.f)};
}

std::vector<math::float3> triangleQuadsSoup(int primitiveCount)
{
  const Grid grid = gridFor(primitiveCount);
  std::vector<math::float3> positions;
  positions.reserve(static_cast<size_t>(primitiveCount) * 6);
  for (int i = 0; i < primitiveCount; ++i) {
    const auto square = squareCorners(cellCenter(i, grid));
    positions.insert(positions.end(),
        {square[0], square[1], square[2], square[0], square[2], square[3]});
  }
  return positions;
}

TriangleLayout triangleQuadsIndexed(int primitiveCount)
{
  const Grid grid = gridFor(primitiveCount);
  TriangleLayout layout;
  layout.positions.reserve(static_cast<size_t>(primitiveCount) * 4);
  layout.indices.reserve(static_cast<size_t>(primitiveCount) * 2);
  for (int i = 0; i < primitiveCount; ++i) {
    const auto square = squareCorners(cellCenter(i, grid));
    const uint32_t base = static_cast<uint32_t>(i) * 4u;
    layout.positions.insert(
        layout.positions.end(), square.begin(), square.end());
    layout.indices.push_back(uint3{base, base + 1u, base + 2u});
    layout.indices.push_back(uint3{base, base + 2u, base + 3u});
  }
  return layout;
}

std::vector<math::float3> triangleCubesSoup(int primitiveCount)
{
  const Grid grid = gridFor(primitiveCount);
  std::vector<math::float3> positions;
  positions.reserve(static_cast<size_t>(primitiveCount) * 36);
  for (int i = 0; i < primitiveCount; ++i) {
    const auto corners = cubeCorners(cellCenter(i, grid));
    for (const auto &triangle : kCubeTriangles) {
      positions.insert(positions.end(),
          {corners[triangle.x], corners[triangle.y], corners[triangle.z]});
    }
  }
  return positions;
}

TriangleLayout triangleCubesIndexed(int primitiveCount)
{
  const Grid grid = gridFor(primitiveCount);
  TriangleLayout layout;
  layout.positions.reserve(static_cast<size_t>(primitiveCount) * 8);
  layout.indices.reserve(static_cast<size_t>(primitiveCount) * 12);
  for (int i = 0; i < primitiveCount; ++i) {
    const auto corners = cubeCorners(cellCenter(i, grid));
    const uint32_t base = static_cast<uint32_t>(i) * 8u;
    layout.positions.insert(
        layout.positions.end(), corners.begin(), corners.end());
    for (const auto &triangle : kCubeTriangles) {
      layout.indices.push_back(
          uint3{triangle.x + base, triangle.y + base, triangle.z + base});
    }
  }
  return layout;
}

std::vector<math::float3> quadsSoup(int primitiveCount)
{
  const Grid grid = gridFor(primitiveCount);
  std::vector<math::float3> positions;
  positions.reserve(static_cast<size_t>(primitiveCount) * 4);
  for (int i = 0; i < primitiveCount; ++i) {
    const auto square = squareCorners(cellCenter(i, grid));
    positions.insert(positions.end(), square.begin(), square.end());
  }
  return positions;
}

std::vector<math::float3> quadCubesSoup(int primitiveCount)
{
  const Grid grid = gridFor(primitiveCount);
  std::vector<math::float3> positions;
  positions.reserve(static_cast<size_t>(primitiveCount) * 24);
  for (int i = 0; i < primitiveCount; ++i) {
    const auto corners = cubeCorners(cellCenter(i, grid));
    for (const auto &quad : kCubeQuads) {
      positions.insert(positions.end(),
          {corners[quad.x], corners[quad.y], corners[quad.z], corners[quad.w]});
    }
  }
  return positions;
}

QuadLayout quadCubesIndexed(int primitiveCount)
{
  const Grid grid = gridFor(primitiveCount);
  QuadLayout layout;
  layout.positions.reserve(static_cast<size_t>(primitiveCount) * 8);
  layout.indices.reserve(static_cast<size_t>(primitiveCount) * 6);
  for (int i = 0; i < primitiveCount; ++i) {
    const auto corners = cubeCorners(cellCenter(i, grid));
    const uint32_t base = static_cast<uint32_t>(i) * 8u;
    layout.positions.insert(
        layout.positions.end(), corners.begin(), corners.end());
    for (const auto &quad : kCubeQuads) {
      layout.indices.push_back(
          uint4{quad.x + base, quad.y + base, quad.z + base, quad.w + base});
    }
  }
  return layout;
}

std::vector<math::float3> spherePositions(int primitiveCount)
{
  const Grid grid = gridFor(primitiveCount);
  std::vector<math::float3> positions;
  positions.reserve(primitiveCount);
  for (int i = 0; i < primitiveCount; ++i)
    positions.push_back(cellCenter(i, grid));
  return positions;
}

std::vector<math::float3> verticalSegments(int primitiveCount)
{
  const Grid grid = gridFor(primitiveCount);
  std::vector<math::float3> positions;
  positions.reserve(static_cast<size_t>(primitiveCount) * 2);
  for (int i = 0; i < primitiveCount; ++i) {
    const math::float3 center = cellCenter(i, grid);
    positions.push_back(center + math::float3(0.f, -kHalf, 0.f));
    positions.push_back(center + math::float3(0.f, kHalf, 0.f));
  }
  return positions;
}

std::vector<math::float3> curveSegments(int primitiveCount)
{
  const Grid grid = gridFor(primitiveCount);
  std::vector<math::float3> positions;
  positions.reserve(static_cast<size_t>(primitiveCount) * 2);
  for (int i = 0; i < primitiveCount; ++i) {
    const int row = i / grid.columns;
    int column = i % grid.columns;
    if (row % 2 == 1)
      column = grid.columns - 1 - column;
    const math::float3 center = cellCenter(row * grid.columns + column, grid);
    const math::float3 low = center + math::float3(0.f, -kHalf, 0.f);
    const math::float3 high = center + math::float3(0.f, kHalf, 0.f);
    if (i % 2 == 0) {
      positions.push_back(low);
      positions.push_back(high);
    } else {
      positions.push_back(high);
      positions.push_back(low);
    }
  }
  return positions;
}

template <typename T>
void dropLastIndex(std::vector<T> &indices, bool unusedVertices)
{
  if (unusedVertices)
    indices.pop_back();
}

} // namespace

PrimitiveMode parsePrimitiveMode(std::string_view mode)
{
  if (mode == "soup")
    return PrimitiveMode::Soup;
  if (mode == "indexed")
    return PrimitiveMode::Indexed;
  throw std::invalid_argument(
      "unknown geometry primitive mode '" + std::string(mode) + "'");
}

std::vector<math::float3> layoutTriangleSoup(int primitiveCount)
{
  validateSpecification(primitiveCount, PrimitiveMode::Soup, false);
  const Grid grid = gridFor(primitiveCount);
  std::vector<math::float3> positions;
  positions.reserve(static_cast<size_t>(primitiveCount) * 3);
  for (int i = 0; i < primitiveCount; ++i) {
    const math::float3 center = cellCenter(i, grid);
    constexpr float kSqrt3Over2 = 0.8660254f;
    positions.push_back(center + math::float3(0.f, kHalf, 0.f));
    positions.push_back(
        center + math::float3(-kHalf * kSqrt3Over2, -kHalf * 0.5f, 0.f));
    positions.push_back(
        center + math::float3(kHalf * kSqrt3Over2, -kHalf * 0.5f, 0.f));
  }
  return positions;
}

TriangleLayout makeTriangleLayout(TriangleShape shape,
    PrimitiveMode mode,
    int primitiveCount,
    bool unusedVertices)
{
  validateSpecification(primitiveCount, mode, unusedVertices);
  TriangleLayout layout;
  switch (shape) {
  case TriangleShape::Triangle:
    layout.positions = layoutTriangleSoup(primitiveCount);
    if (mode == PrimitiveMode::Indexed) {
      layout.indices.reserve(primitiveCount);
      for (size_t i = 0; i < layout.positions.size(); i += 3) {
        const auto index = static_cast<uint32_t>(i);
        layout.indices.push_back(uint3{index, index + 1u, index + 2u});
      }
    }
    break;
  case TriangleShape::Quad:
    if (mode == PrimitiveMode::Indexed)
      layout = triangleQuadsIndexed(primitiveCount);
    else
      layout.positions = triangleQuadsSoup(primitiveCount);
    break;
  case TriangleShape::Cube:
    if (mode == PrimitiveMode::Indexed)
      layout = triangleCubesIndexed(primitiveCount);
    else
      layout.positions = triangleCubesSoup(primitiveCount);
    break;
  default:
    throw std::invalid_argument("invalid triangle shape enum value");
  }
  dropLastIndex(layout.indices, unusedVertices);
  return layout;
}

QuadLayout makeQuadLayout(QuadShape shape,
    PrimitiveMode mode,
    int primitiveCount,
    bool unusedVertices)
{
  validateSpecification(primitiveCount, mode, unusedVertices);
  QuadLayout layout;
  switch (shape) {
  case QuadShape::Quad:
    layout.positions = quadsSoup(primitiveCount);
    if (mode == PrimitiveMode::Indexed) {
      layout.indices.reserve(primitiveCount);
      for (size_t i = 0; i < layout.positions.size(); i += 4) {
        const auto index = static_cast<uint32_t>(i);
        layout.indices.push_back(
            uint4{index, index + 1u, index + 2u, index + 3u});
      }
    }
    break;
  case QuadShape::Cube:
    if (mode == PrimitiveMode::Indexed)
      layout = quadCubesIndexed(primitiveCount);
    else
      layout.positions = quadCubesSoup(primitiveCount);
    break;
  default:
    throw std::invalid_argument("invalid quad shape enum value");
  }
  dropLastIndex(layout.indices, unusedVertices);
  return layout;
}

SphereLayout makeSphereLayout(
    PrimitiveMode mode, int primitiveCount, bool unusedVertices)
{
  validateSpecification(primitiveCount, mode, unusedVertices);
  SphereLayout layout;
  layout.positions = spherePositions(primitiveCount);
  if (mode == PrimitiveMode::Indexed) {
    layout.indices.reserve(layout.positions.size());
    for (size_t i = 0; i < layout.positions.size(); ++i)
      layout.indices.push_back(static_cast<uint32_t>(i));
  }
  dropLastIndex(layout.indices, unusedVertices);
  return layout;
}

CurveLayout makeCurveLayout(
    PrimitiveMode mode, int primitiveCount, bool unusedVertices)
{
  validateSpecification(primitiveCount, mode, unusedVertices);
  CurveLayout layout;
  layout.positions = curveSegments(primitiveCount);
  if (mode == PrimitiveMode::Indexed) {
    layout.indices.reserve(primitiveCount);
    for (uint32_t i = 0; i < layout.positions.size() / 2; ++i)
      layout.indices.push_back(i * 2);
  }
  dropLastIndex(layout.indices, unusedVertices);
  return layout;
}

SegmentLayout makeSegmentLayout(
    PrimitiveMode mode, int primitiveCount, bool unusedVertices)
{
  validateSpecification(primitiveCount, mode, unusedVertices);
  SegmentLayout layout;
  layout.positions = verticalSegments(primitiveCount);
  if (mode == PrimitiveMode::Indexed) {
    layout.indices.reserve(primitiveCount);
    for (uint32_t i = 0; i < layout.positions.size(); i += 2)
      layout.indices.push_back(uint2{i, i + 1});
  }
  dropLastIndex(layout.indices, unusedVertices);
  return layout;
}

} // namespace cts
} // namespace anari
