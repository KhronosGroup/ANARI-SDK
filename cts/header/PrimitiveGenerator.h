// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp/ext/linalg.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <random>
#include <tuple>

namespace cts {

class PrimitiveGenerator
{
 public:
  PrimitiveGenerator(int seed);

  // triangles
  std::vector<anari::math::float3> generateTriangles(size_t primitiveCount);
  std::vector<anari::math::float3> generateTriangulatedQuadsSoup(size_t primitiveCount);
  std::tuple<std::vector<anari::math::float3>, std::vector<anari::math::vec<uint32_t, 3>>>
  generateTriangulatedQuadsIndexed(size_t primitiveCount);
  std::vector<anari::math::float3> generateTriangulatedCubesSoup(size_t primitiveCount);
  std::tuple <std::vector<anari::math::float3>,
      std::vector<anari::math::vec<uint32_t, 3>>>
  generateTriangulatedCubesIndexed(size_t primitiveCount);

  // quads
  std::vector<anari::math::float3> generateQuads(size_t primitiveCount);
  std::vector<anari::math::float3> generateQuadCubesSoup(size_t primitiveCount);
  std::tuple<std::vector<anari::math::float3>,
      std::vector<anari::math::vec<uint32_t, 4>>>
  generateQuadCubesIndexed(size_t primitiveCount);

  // others
  std::tuple<std::vector<anari::math::float3>, std::vector<float>> generateSpheres(
      size_t primitiveCount);
  std::tuple<std::vector<anari::math::float3>, std::vector<float>> generateCurves(
      size_t primitiveCount);
  std::tuple<std::vector<anari::math::float3>, std::vector<float>, std::vector<uint8_t>>
  generateCones(size_t primitiveCount, std::optional<int32_t> hasVertexCap);
  std::tuple<std::vector<anari::math::float3>, std::vector<float>, std::vector<uint8_t>>
  generateCylinders(size_t primitiveCount, std::optional<int32_t> hasVertexCap);

  std::vector<float> generateAttributeFloat(
      size_t elementCount, float min = 0.0f, float max = 1.0f);
  std::vector<anari::math::float2> generateAttributeVec2(
      size_t elementCount, float min = 0.0f, float max = 1.0f);
  std::vector<anari::math::float3> generateAttributeVec3(
      size_t elementCount, float min = 0.0f, float max = 1.0f);
  std::vector<anari::math::float4> generateAttributeVec4(
      size_t elementCount, float min = 0.0f, float max = 1.0f);

  template <typename T>
  void shuffleVector(std::vector<T> &vector)
  {
    std::shuffle(vector.begin(), vector.end(), m_rng);
  }

  float getRandomFloat(float min, float max);
  anari::math::float2 getRandomVector2(float min, float max);
  anari::math::float3 getRandomVector3(float min, float max);
  anari::math::float4 getRandomVector4(float min, float max);
  std::vector<anari::math::float3> randomTranslate(
      std::vector<anari::math::float3> vertices, size_t verticesPerObject);
  std::vector<anari::math::float3> randomTransform(
      std::vector<anari::math::float3> vertices, size_t verticesPerObject);

 private:
  std::mt19937 m_rng;
};

} // namespace cts
