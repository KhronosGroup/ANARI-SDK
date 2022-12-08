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
  std::vector<glm::vec4> generateAttribute(size_t elementCount, float min = 0.0f, float max = 1.0f);

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
  void shuffleVector(std::vector<T> &vector)
  {
    std::shuffle(vector.begin(), vector.end(), m_rng);
  }

 private:
  std::mt19937 m_rng;

  float getRandomFloat(float min, float max);
  glm::vec3 getRandomVector3(float min, float max);
  glm::vec4 getRandomVector4(float min, float max);
  std::vector<glm::vec3> randomTranslate(std::vector<glm::vec3> vertices, size_t verticesPerObject);
  std::vector<glm::vec3> randomTransform(std::vector<glm::vec3> vertices, size_t verticesPerObject);
};

} // namespace cts
