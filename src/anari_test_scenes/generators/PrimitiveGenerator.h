// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp/ext/linalg.h"
#include "anari_test_scenes_export.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <random>
#include <tuple>

namespace anari {
namespace scenes {
class PrimitiveGenerator
{
 public:
  ANARI_TEST_SCENES_INTERFACE PrimitiveGenerator(int seed);

  // triangles
  ANARI_TEST_SCENES_INTERFACE std::vector<anari::math::float3>
  generateTriangles(size_t primitiveCount);
  ANARI_TEST_SCENES_INTERFACE std::vector<anari::math::float3>
  generateTriangulatedQuadsSoup(size_t primitiveCount);
  ANARI_TEST_SCENES_INTERFACE std::tuple<std::vector<anari::math::float3>,
      std::vector<anari::math::vec<uint32_t, 3>>>
  generateTriangulatedQuadsIndexed(size_t primitiveCount);
  ANARI_TEST_SCENES_INTERFACE std::vector<anari::math::float3>
  generateTriangulatedCubesSoup(size_t primitiveCount);
  ANARI_TEST_SCENES_INTERFACE std::tuple<std::vector<anari::math::float3>,
      std::vector<anari::math::vec<uint32_t, 3>>>
  generateTriangulatedCubesIndexed(size_t primitiveCount);

  // quads
  ANARI_TEST_SCENES_INTERFACE std::vector<anari::math::float3> generateQuads(
      size_t primitiveCount);
  ANARI_TEST_SCENES_INTERFACE std::vector<anari::math::float3>
  generateQuadCubesSoup(size_t primitiveCount);
  ANARI_TEST_SCENES_INTERFACE std::tuple<std::vector<anari::math::float3>,
      std::vector<anari::math::vec<uint32_t, 4>>>
  generateQuadCubesIndexed(size_t primitiveCount);

  // others
  ANARI_TEST_SCENES_INTERFACE
  std::tuple<std::vector<anari::math::float3>, std::vector<float>>
  generateSpheres(size_t primitiveCount);
  ANARI_TEST_SCENES_INTERFACE
  std::tuple<std::vector<anari::math::float3>, std::vector<float>>
  generateCurves(size_t primitiveCount);
  ANARI_TEST_SCENES_INTERFACE std::tuple<std::vector<anari::math::float3>,
      std::vector<float>,
      std::vector<uint8_t>>
  generateCones(size_t primitiveCount, std::optional<int32_t> hasVertexCap);
  ANARI_TEST_SCENES_INTERFACE std::tuple<std::vector<anari::math::float3>,
      std::vector<float>,
      std::vector<uint8_t>>
  generateCylinders(size_t primitiveCount, std::optional<int32_t> hasVertexCap);

  ANARI_TEST_SCENES_INTERFACE std::vector<float> generateAttributeFloat(
      size_t elementCount, float min = 0.0f, float max = 1.0f);
  ANARI_TEST_SCENES_INTERFACE std::vector<anari::math::float2>
  generateAttributeVec2(
      size_t elementCount, float min = 0.0f, float max = 1.0f);
  ANARI_TEST_SCENES_INTERFACE std::vector<anari::math::float3>
  generateAttributeVec3(
      size_t elementCount, float min = 0.0f, float max = 1.0f);
  ANARI_TEST_SCENES_INTERFACE std::vector<anari::math::float4>
  generateAttributeVec4(
      size_t elementCount, float min = 0.0f, float max = 1.0f);

  template <typename T>
  ANARI_TEST_SCENES_INTERFACE void shuffleVector(std::vector<T> &vector)
  {
    std::shuffle(vector.begin(), vector.end(), m_rng);
  }

  ANARI_TEST_SCENES_INTERFACE float getRandomFloat(float min, float max);
  ANARI_TEST_SCENES_INTERFACE anari::math::float2 getRandomVector2(
      float min, float max);
  ANARI_TEST_SCENES_INTERFACE anari::math::float3 getRandomVector3(
      float min, float max);
  ANARI_TEST_SCENES_INTERFACE anari::math::float4 getRandomVector4(
      float min, float max);
  ANARI_TEST_SCENES_INTERFACE std::vector<anari::math::float3> randomTranslate(
      std::vector<anari::math::float3> vertices, size_t verticesPerObject);
  ANARI_TEST_SCENES_INTERFACE std::vector<anari::math::float3> randomTransform(
      std::vector<anari::math::float3> vertices, size_t verticesPerObject);

 private:
  std::mt19937 m_rng;
};

} // namespace scenes
} // namespace anari
