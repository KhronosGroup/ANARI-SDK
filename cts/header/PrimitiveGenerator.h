// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp/ext/glm.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <tuple>

namespace cts {

class PrimitiveGenerator
{
 public:
  PrimitiveGenerator(int seed);

  // triangles
  std::vector<glm::vec3> generateTriangles(size_t primitiveCount);
  std::vector<glm::vec3> generateTriangulatedQuadsSoup(size_t primitiveCount);
  std::tuple<std::vector<glm::vec3>, std::vector<glm::uvec3>>
  generateTriangulatedQuadsIndexed(size_t primitiveCount);
  std::vector<glm::vec3> generateTriangulatedCubesSoup(size_t primitiveCount);
  std::tuple<std::vector<glm::vec3>, std::vector<glm::uvec3>>
  generateTriangulatedCubesIndexed(size_t primitiveCount);

  // quads
  std::vector<glm::vec3> generateQuads(size_t primitiveCount);
  std::vector<glm::vec3> generateQuadCubesSoup(size_t primitiveCount);
  std::tuple<std::vector<glm::vec3>, std::vector<glm::uvec4>>
  generateQuadCubesIndexed(size_t primitiveCount);

  // others
  std::tuple<std::vector<glm::vec3>, std::vector<float>> generateSpheres(
      size_t primitiveCount);
  std::tuple<std::vector<glm::vec3>, std::vector<float>> generateCurves(
      size_t primitiveCount);
  std::tuple<std::vector<glm::vec3>, std::vector<float>> generateCones(
      size_t primitiveCount);
  std::tuple<std::vector<glm::vec3>, std::vector<float>> generateCylinders(
      size_t primitiveCount);

  template <typename T>
  std::vector<T> shuffleVector(std::vector<T> vector)
  {
    size_t size = vector.size();
    size_t counter = 0;
    for (auto it = vector.begin(); it != vector.end(); ++it) {
      size_t randomIndex = static_cast<size_t>(std::round(getRandomFloat(0, static_cast<float>(size - 1))));
      std::iter_swap(it, it + (randomIndex - counter)); 
      ++counter;
    }
    return vector;
  }

 private:
  std::mt19937 m_rng;

  float getRandomFloat(float min, float max);
  glm::vec3 getRandomVector(float min, float max);
  std::vector<glm::vec3> randomTranslate(std::vector<glm::vec3> vertices, size_t verticesPerObject);
  std::vector<glm::vec3> randomTransform(std::vector<glm::vec3> vertices, size_t verticesPerObject);
};

} // namespace cts
