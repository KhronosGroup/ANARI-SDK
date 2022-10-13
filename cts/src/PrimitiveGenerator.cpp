// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "PrimitiveGenerator.h"

#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <iterator>

namespace cts {

PrimitiveGenerator::PrimitiveGenerator(int seed)
{
  m_rng.seed(seed);
}

float PrimitiveGenerator::getRandom(float min, float max)
{
  std::uniform_real_distribution<float> uniformDist(min, max);

  return uniformDist(m_rng);
}

std::vector<glm::vec3> PrimitiveGenerator::generateTriangles(
    size_t primitiveCount)
{
  std::vector<glm::vec3> vertices((primitiveCount * 3));
  for (auto& vertex : vertices) {
    vertex.x = getRandom(0.0f, 1.0f);
    vertex.y = getRandom(0.0f, 1.0f);
    vertex.z = getRandom(0.0f, 1.0f);
  }

  // add translation offset per triangle
  for (size_t i = 0; i < vertices.size() - 2; i += 3) {
    glm::vec3 offset(getRandom(0.0f, 0.6f), getRandom(0.0f, 0.6f), getRandom(0.0f, 0.6f));
    vertices[i] = (vertices[i] * 0.4f) + offset;
    vertices[i + 1] = (vertices[i + 1] * 0.4f) + offset;
    vertices[i + 2] = (vertices[i + 2] * 0.4f) + offset;
  }

  return vertices;
}

std::vector<glm::vec3> PrimitiveGenerator::generateTriangulatedQuadSoups(
    size_t primitiveCount)
{
  std::vector<glm::vec3> vertices((primitiveCount * 6));
  size_t i = 0;
  glm::vec3 vertex0(0), vertex1(0), vertex2(0);
  for (auto &vertex : vertices) {
    if (i == 3) {
      vertex = vertex2;
    } else if (i == 4) {
      vertex = vertex1;
    } else {
      vertex.x = getRandom(0.0f, 1.0f);
      vertex.y = getRandom(0.0f, 1.0f);
      vertex.z = getRandom(0.0f, 1.0f);
    }

    if (i == 0) {
      vertex0 = vertex;
    } else if (i == 1) {
      vertex1 = vertex;
    } else if (i == 2) {
      vertex2 = vertex;
    } else if (i == 5) {
      glm::vec3 vec01 = vertex1 - vertex0;
      vertex = vertex2 + vec01;
    }

    i = (i + 1) % 6;
  }

  // add translation offset per quad
  for (size_t k = 0; k < vertices.size() - 5; k += 6) {
    glm::vec3 offset(
        getRandom(0.0f, 0.6f), getRandom(0.0f, 0.6f), getRandom(0.0f, 0.6f));
    vertices[k] = (vertices[k] * 0.4f) + offset;
    vertices[k + 1] = (vertices[k + 1] * 0.4f) + offset;
    vertices[k + 2] = (vertices[k + 2] * 0.4f) + offset;
    vertices[k + 3] = (vertices[k + 3] * 0.4f) + offset;
    vertices[k + 4] = (vertices[k + 4] * 0.4f) + offset;
    vertices[k + 5] = (vertices[k + 5] * 0.4f) + offset;
  }

  return vertices;
}

std::vector<glm::vec3> PrimitiveGenerator::generateTriangulatedCubeSoups(
    size_t primitiveCount)
{
  std::vector<glm::vec3> vertices;
  std::vector<glm::vec3> cubeVertices{
    {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, // front
    {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 1.0, 0.0},
    {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, {1.0, 0.0, 1.0}, // right
    {1.0, 0.0, 1.0}, {1.0, 1.0, 0.0}, {1.0, 1.0, 1.0},
    {1.0, 0.0, 1.0}, {1.0, 1.0, 1.0}, {0.0, 0.0, 1.0}, // back
    {0.0, 1.0, 1.0}, {1.0, 1.0, 1.0}, {0.0, 0.0, 1.0},
    {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, 1.0, 1.0}, // left
    {0.0, 0.0, 0.0}, {0.0, 1.0, 1.0}, {0.0, 1.0, 0.0},
    {0.0, 1.0, 0.0}, {1.0, 1.0, 1.0}, {1.0, 1.0, 0.0}, // top
    {0.0, 1.0, 0.0}, {0.0, 1.0, 1.0}, {1.0, 1.0, 1.0},
    {0.0, 0.0, 0.0}, {1.0, 0.0, 1.0}, {1.0, 0.0, 0.0}, // bottom
    {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {1.0, 0.0, 1.0}};

  for (size_t i = 0; i < primitiveCount; ++i) {
    std::copy(cubeVertices.begin(),
        cubeVertices.end(),
        std::back_insert_iterator(vertices));
  }

  // add random transform per cube
  for (size_t k = 0; k < primitiveCount; ++k) {
    float cubeScale = getRandom(0.0f, 0.4f);

    glm::mat4 scale = glm::scale(glm::mat4(1.0f),
        glm::vec3(cubeScale, cubeScale, cubeScale));

    glm::mat4 rotation = glm::rotate(
        glm::mat4(1.0f), getRandom(0.0f, 360.0f), 
        glm::vec3(getRandom(0.0f, 1.0f), getRandom(0.0f, 1.0f), getRandom(0.0f, 1.0f)));

    glm::mat4 translation = glm::translate(glm::mat4(1.0f),
        glm::vec3(getRandom(0.0f, 0.6f),
            getRandom(0.0f, 0.6f),
            getRandom(0.0f, 0.6f)));

    glm::mat4 transform = translation * rotation * scale;

    for (size_t i = 0; i < 36; ++i) {
      const size_t index = i + 36 * k;
      vertices[index] = transform * glm::vec4(vertices[index], 1.0);
    }
  }

  return vertices;
}

std::tuple<std::vector<glm::vec3>, std::vector<glm::ivec3>> PrimitiveGenerator::generateTriangulatedCubesIndexed(
    size_t primitiveCount)
{
  std::vector<glm::vec3> vertices;
  std::vector<glm::ivec3> indices;
  std::vector<glm::vec3> cubeVertices {
    {0.0, 0.0, 0.0}, 
    {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, 
    {1.0, 1.0, 0.0}, {1.0, 0.0, 1.0}, {0.0, 1.0, 1.0}, 
    {1.0, 1.0, 1.0}};
  std::vector<glm::ivec3> cubeIndices {
    {0, 2, 1}, {1, 2, 4}, // front
    {1, 4, 5}, {5, 4, 7}, // right
    {5, 7, 3}, {6, 7, 3}, // back
    {0, 3, 6}, {0, 6, 2}, // left
    {2, 7, 4}, {2, 6, 7}, // top
    {0, 5, 1}, {0, 3, 5}  // bottom
  };

  for (size_t i = 0; i < primitiveCount; ++i) {
    std::copy(cubeVertices.begin(),
        cubeVertices.end(),
        std::back_insert_iterator(vertices));

    std::vector<glm::ivec3> newIndices = cubeIndices;

    for (auto &indexVector : newIndices) {
      indexVector += glm::ivec3(static_cast<int>(i) * 8);
    }
  }

  // add random transform per cube
  for (size_t k = 0; k < primitiveCount; ++k) {
    float cubeScale = getRandom(0.0f, 0.4f);

    glm::mat4 scale =
        glm::scale(glm::mat4(1.0f), glm::vec3(cubeScale, cubeScale, cubeScale));

    glm::mat4 rotation = glm::rotate(glm::mat4(1.0f),
        getRandom(0.0f, 360.0f),
        glm::vec3(getRandom(0.0f, 1.0f),
            getRandom(0.0f, 1.0f),
            getRandom(0.0f, 1.0f)));

    glm::mat4 translation = glm::translate(glm::mat4(1.0f),
        glm::vec3(getRandom(0.0f, 0.6f),
            getRandom(0.0f, 0.6f),
            getRandom(0.0f, 0.6f)));

    glm::mat4 transform = translation * rotation * scale;

    for (size_t i = 0; i < 8; ++i) {
      const size_t index = i + 8 * k;
      vertices[index] = transform * glm::vec4(vertices[index], 1.0);
    }
  }

  return std::make_tuple(vertices, indices);
}

} // namespace cts