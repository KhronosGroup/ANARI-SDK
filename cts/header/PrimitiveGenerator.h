// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp/ext/glm.h"

#include <random>
#include <tuple>

namespace cts {

class PrimitiveGenerator
{
 public:
  PrimitiveGenerator(int seed);

  std::vector<glm::vec3> generateTriangles(size_t primitiveCount);
  std::vector<glm::vec3> generateTriangulatedQuadSoups(size_t primitiveCount);
  std::tuple<std::vector<glm::vec3>, std::vector<glm::uvec3>>
  generateTriangulatedQuadsIndexed(size_t primitiveCount);
  std::vector<glm::vec3> generateTriangulatedCubeSoups(size_t primitiveCount);
  std::tuple<std::vector<glm::vec3>, std::vector<glm::uvec3>>
  generateTriangulatedCubesIndexed(size_t primitiveCount);

  std::vector<glm::vec3> generateQuads(size_t primitiveCount);
  std::vector<glm::vec3> generateQuadCubeSoups(size_t primitiveCount);
 private:
  std::mt19937 m_rng;

  float getRandom(float min, float max);
};

} // namespace cts
