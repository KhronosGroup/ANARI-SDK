// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "GeometryLayout.h"
// std
#include <optional>
#include <string_view>

namespace anari {
namespace cts {

enum class ColorAttribute
{
  None,
  Vertex,
  Primitive,
};

enum class CapsMode
{
  None,
  First,
  Second,
  Both,
};

struct GeometryAttributes
{
  ColorAttribute color{ColorAttribute::None};
  bool vertexRamps{false};
  bool primitiveRamps{false};
  float rampMinimum{0.f};
  float rampMaximum{1.f};
};

struct TriangleSpec
{
  TriangleShape shape{TriangleShape::Triangle};
  PrimitiveMode mode{PrimitiveMode::Soup};
  int primitiveCount{1};
  bool unusedVertices{false};
  GeometryAttributes attributes;
};

struct QuadSpec
{
  QuadShape shape{QuadShape::Quad};
  PrimitiveMode mode{PrimitiveMode::Soup};
  int primitiveCount{1};
  bool unusedVertices{false};
  GeometryAttributes attributes;
};

struct SphereSpec
{
  PrimitiveMode mode{PrimitiveMode::Soup};
  int primitiveCount{1};
  bool unusedVertices{false};
  std::optional<float> globalRadius;
  GeometryAttributes attributes;
};

struct CurveSpec
{
  PrimitiveMode mode{PrimitiveMode::Soup};
  int primitiveCount{1};
  bool unusedVertices{false};
  std::optional<float> globalRadius;
  GeometryAttributes attributes;
};

struct ConeSpec
{
  PrimitiveMode mode{PrimitiveMode::Soup};
  int primitiveCount{1};
  bool unusedVertices{false};
  std::optional<bool> vertexCaps;
  CapsMode caps{CapsMode::None};
  GeometryAttributes attributes;
};

struct CylinderSpec
{
  PrimitiveMode mode{PrimitiveMode::Soup};
  int primitiveCount{1};
  bool unusedVertices{false};
  std::optional<float> globalRadius;
  std::optional<bool> vertexCaps;
  CapsMode caps{CapsMode::None};
  GeometryAttributes attributes;
};

ColorAttribute parseColorAttribute(
    std::string_view attribute);
CapsMode parseCapsMode(std::string_view mode);

anari::Geometry buildTriangleGeometry(
    anari::Device d, const TriangleSpec &spec);
anari::Geometry buildQuadGeometry(
    anari::Device d, const QuadSpec &spec);
anari::Geometry buildSphereGeometry(
    anari::Device d, const SphereSpec &spec);
anari::Geometry buildCurveGeometry(
    anari::Device d, const CurveSpec &spec);
anari::Geometry buildConeGeometry(
    anari::Device d, const ConeSpec &spec);
anari::Geometry buildCylinderGeometry(
    anari::Device d, const CylinderSpec &spec);

} // namespace cts
} // namespace anari
