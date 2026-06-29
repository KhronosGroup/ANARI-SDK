// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "GeometryLayout.h"
#include "anari_test_scenes_export.h"
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

ANARI_TEST_SCENES_INTERFACE ColorAttribute parseColorAttribute(
    std::string_view attribute);
ANARI_TEST_SCENES_INTERFACE CapsMode parseCapsMode(std::string_view mode);

ANARI_TEST_SCENES_INTERFACE anari::Geometry buildTriangleGeometry(
    anari::Device d, const TriangleSpec &spec);
ANARI_TEST_SCENES_INTERFACE anari::Geometry buildQuadGeometry(
    anari::Device d, const QuadSpec &spec);
ANARI_TEST_SCENES_INTERFACE anari::Geometry buildSphereGeometry(
    anari::Device d, const SphereSpec &spec);
ANARI_TEST_SCENES_INTERFACE anari::Geometry buildCurveGeometry(
    anari::Device d, const CurveSpec &spec);
ANARI_TEST_SCENES_INTERFACE anari::Geometry buildConeGeometry(
    anari::Device d, const ConeSpec &spec);
ANARI_TEST_SCENES_INTERFACE anari::Geometry buildCylinderGeometry(
    anari::Device d, const CylinderSpec &spec);

} // namespace cts
} // namespace anari
