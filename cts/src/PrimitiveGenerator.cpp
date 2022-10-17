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
  std::vector<glm::vec3> vertices(primitiveCount * 3);
  for (auto& vertex : vertices) {
    vertex.x = getRandom(0.0f, 1.0f);
    vertex.y = getRandom(0.0f, 1.0f);
    vertex.z = getRandom(0.0f, 1.0f);
  }

  // add translation offset per triangle
  for (size_t i = 0; i < vertices.size() - 2; i += 3) {
    const glm::vec3 offset(getRandom(0.0f, 0.6f), getRandom(0.0f, 0.6f), getRandom(0.0f, 0.6f));
    vertices[i] = (vertices[i] * 0.4f) + offset;
    vertices[i + 1] = (vertices[i + 1] * 0.4f) + offset;
    vertices[i + 2] = (vertices[i + 2] * 0.4f) + offset;
  }

  return vertices;
}

std::vector<glm::vec3> PrimitiveGenerator::generateTriangulatedQuadSoups(
    size_t primitiveCount)
{
  std::vector<glm::vec3> vertices(primitiveCount * 6);
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
      const glm::vec3 vec01 = vertex1 - vertex0;
      vertex = vertex2 + vec01;
    }

    i = (i + 1) % 6;
  }

  // add translation offset per quad
  for (size_t k = 0; k < primitiveCount; ++k) {
    const size_t index = k * 6;
    const glm::vec3 offset(
        getRandom(0.0f, 0.6f), getRandom(0.0f, 0.6f), getRandom(0.0f, 0.6f));
    vertices[index] = (vertices[index] * 0.4f) + offset;
    vertices[index + 1] = (vertices[index + 1] * 0.4f) + offset;
    vertices[index + 2] = (vertices[index + 2] * 0.4f) + offset;
    vertices[index + 3] = (vertices[index + 3] * 0.4f) + offset;
    vertices[index + 4] = (vertices[index + 4] * 0.4f) + offset;
    vertices[index + 5] = (vertices[index + 5] * 0.4f) + offset;
  }

  return vertices;
}

std::tuple<std::vector<glm::vec3>, std::vector<glm::uvec3>> PrimitiveGenerator::generateTriangulatedQuadsIndexed(
    size_t primitiveCount)
{
  std::vector<glm::vec3> vertices(primitiveCount * 4);
  std::vector<glm::uvec3> indices(primitiveCount * 2);
  size_t i = 0;
  glm::vec3 vertex0(0), vertex1(0), vertex2(0);
  for (auto &vertex : vertices) {
    if (i == 3) {
      const glm::vec3 vec01 = vertex1 - vertex0;
      vertex = vertex2 + vec01;
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
    } 

    i = (i + 1) % 4;
  }

  // add translation offset per quad
  for (size_t k = 0; k < primitiveCount; ++k) {
    const size_t index = k * 4;
    const glm::vec3 offset(
        getRandom(0.0f, 0.6f), getRandom(0.0f, 0.6f), getRandom(0.0f, 0.6f));
    vertices[index] = (vertices[index] * 0.4f) + offset;
    vertices[index + 1] = (vertices[index + 1] * 0.4f) + offset;
    vertices[index + 2] = (vertices[index + 2] * 0.4f) + offset;
    vertices[index + 3] = (vertices[index + 3] * 0.4f) + offset;

    const size_t indicesIndex = k * 2;
    // fill indices
    indices[indicesIndex] = glm::uvec3(index, index + 1, index + 2);
    indices[indicesIndex + 1] = glm::uvec3(index + 2, index + 1, index + 3);
  }

  return std::make_tuple(vertices, indices);
}

std::vector<glm::vec3> PrimitiveGenerator::generateTriangulatedCubeSoups(
    size_t primitiveCount)
{
  std::vector<glm::vec3> vertices;
  const std::vector<glm::vec3> cubeVertices{
    {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, // front
    {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 1.0, 0.0},
    {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, {1.0, 0.0, 1.0}, // right
    {1.0, 0.0, 1.0}, {1.0, 1.0, 0.0}, {1.0, 1.0, 1.0},
    {1.0, 0.0, 1.0}, {1.0, 1.0, 1.0}, {0.0, 0.0, 1.0}, // back
    {0.0, 1.0, 1.0}, {1.0, 1.0, 1.0}, {0.0, 0.0, 1.0},
    {0.0, 0.0, 0.0}, {0.0, 1.0, 1.0}, {0.0, 0.0, 1.0}, // left
    {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 1.0, 1.0},
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

    const glm::mat4 scale = glm::scale(glm::mat4(1.0f),
        glm::vec3(cubeScale, cubeScale, cubeScale));

    const glm::mat4 rotation = glm::rotate(
        glm::mat4(1.0f), getRandom(0.0f, 360.0f), 
        glm::vec3(getRandom(0.0f, 1.0f), getRandom(0.0f, 1.0f), getRandom(0.0f, 1.0f)));

    const glm::mat4 translation = glm::translate(glm::mat4(1.0f),
        glm::vec3(getRandom(0.0f, 0.6f),
            getRandom(0.0f, 0.6f),
            getRandom(0.0f, 0.6f)));

    const glm::mat4 transform = translation * rotation * scale;

    for (size_t i = 0; i < cubeVertices.size(); ++i) {
      const size_t index = i + cubeVertices.size() * k;
      vertices[index] = transform * glm::vec4(vertices[index], 1.0);
    }
  }

  return vertices;
}

std::tuple<std::vector<glm::vec3>, std::vector<glm::uvec3>> PrimitiveGenerator::generateTriangulatedCubesIndexed(
    size_t primitiveCount)
{
  std::vector<glm::vec3> vertices;
  std::vector<glm::uvec3> indices;
  const std::vector<glm::vec3> cubeVertices {
    {0.0, 0.0, 0.0}, 
    {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, 
    {1.0, 1.0, 0.0}, {1.0, 0.0, 1.0}, {0.0, 1.0, 1.0}, 
    {1.0, 1.0, 1.0}};
  const std::vector<glm::uvec3> cubeIndices {
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

    std::vector<glm::uvec3> newIndices = cubeIndices;

    for (auto &indexVector : newIndices) {
      indexVector += glm::uvec3(static_cast<int>(i * cubeVertices.size()));
    }

    std::copy(newIndices.begin(),
        newIndices.end(),
        std::back_insert_iterator(indices));
  }

  // add random transform per cube
  for (size_t k = 0; k < primitiveCount; ++k) {
    float cubeScale = getRandom(0.0f, 0.4f);

    const glm::mat4 scale =
        glm::scale(glm::mat4(1.0f), glm::vec3(cubeScale, cubeScale, cubeScale));

    const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f),
        getRandom(0.0f, 360.0f),
        glm::vec3(getRandom(0.0f, 1.0f),
            getRandom(0.0f, 1.0f),
            getRandom(0.0f, 1.0f)));

    const glm::mat4 translation = glm::translate(glm::mat4(1.0f),
        glm::vec3(getRandom(0.0f, 0.6f),
            getRandom(0.0f, 0.6f),
            getRandom(0.0f, 0.6f)));

    const glm::mat4 transform = translation * rotation * scale;

    for (size_t i = 0; i < cubeVertices.size(); ++i) {
      const size_t index = i + cubeVertices.size() * k;
      vertices[index] = transform * glm::vec4(vertices[index], 1.0);
    }
  }

  return std::make_tuple(vertices, indices);
}

std::vector<glm::vec3> PrimitiveGenerator::generateQuads(size_t primitiveCount) 
  {
    std::vector<glm::vec3> vertices(primitiveCount * 4);
    for (auto &vertex : vertices) {
      vertex.x = getRandom(0.0f, 1.0f);
      vertex.y = getRandom(0.0f, 1.0f);
      vertex.z = getRandom(0.0f, 1.0f);
    }

    // add translation offset per quad
    for (size_t i = 0; i < primitiveCount; ++i) {
      const size_t index = i * 4;
      const glm::vec3 offset(
          getRandom(0.0f, 0.6f), getRandom(0.0f, 0.6f), getRandom(0.0f, 0.6f));
      vertices[index] = (vertices[index] * 0.4f) + offset;
      vertices[index + 1] = (vertices[index + 1] * 0.4f) + offset;
      vertices[index + 2] = (vertices[index + 2] * 0.4f) + offset;
      vertices[index + 3] = (vertices[index + 3] * 0.4f) + offset;
    }

    return vertices;
  }

std::vector<glm::vec3> PrimitiveGenerator::generateQuadCubeSoups(
      size_t primitiveCount) 
  {
  std::vector<glm::vec3> vertices;
  const std::vector<glm::vec3> cubeVertices{
    {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, // front
    {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, {1.0, 1.0, 1.0}, {1.0, 0.0, 1.0}, // right
    {1.0, 0.0, 1.0}, {1.0, 1.0, 1.0}, {0.0, 1.0, 1.0}, {0.0, 0.0, 1.0}, // back
    {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 1.0, 1.0}, {0.0, 0.0, 1.0}, // left
    {0.0, 1.0, 0.0}, {0.0, 1.0, 1.0}, {1.0, 1.0, 1.0}, {1.0, 1.0, 0.0}, // top
    {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 0.0, 1.0}, {0.0, 0.0, 1.0}};// bottom

  for (size_t i = 0; i < primitiveCount; ++i) {
    std::copy(cubeVertices.begin(),
        cubeVertices.end(),
        std::back_insert_iterator(vertices));
  }

  // add random transform per cube
  for (size_t k = 0; k < primitiveCount; ++k) {
    float cubeScale = getRandom(0.0f, 0.4f);

    const glm::mat4 scale =
        glm::scale(glm::mat4(1.0f), glm::vec3(cubeScale, cubeScale, cubeScale));

    const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f),
        getRandom(0.0f, 360.0f),
        glm::vec3(getRandom(0.0f, 1.0f),
            getRandom(0.0f, 1.0f),
            getRandom(0.0f, 1.0f)));

    const glm::mat4 translation = glm::translate(glm::mat4(1.0f),
        glm::vec3(getRandom(0.0f, 0.6f),
            getRandom(0.0f, 0.6f),
            getRandom(0.0f, 0.6f)));

    const glm::mat4 transform = translation * rotation * scale;

    for (size_t i = 0; i < cubeVertices.size(); ++i) {
      const size_t index = i + cubeVertices.size() * k;
      vertices[index] = transform * glm::vec4(vertices[index], 1.0);
    }
  }

  return vertices;
  }

std::tuple<std::vector<glm::vec3>, std::vector<glm::uvec4>>
  PrimitiveGenerator::generateQuadCubesIndexed(size_t primitiveCount)
  {
    std::vector<glm::vec3> vertices;
    std::vector<glm::uvec4> indices;
    const std::vector<glm::vec3> cubeVertices{{0.0, 0.0, 0.0},
        {1.0, 0.0, 0.0},
        {0.0, 1.0, 0.0},
        {0.0, 0.0, 1.0},
        {1.0, 1.0, 0.0},
        {1.0, 0.0, 1.0},
        {0.0, 1.0, 1.0},
        {1.0, 1.0, 1.0}};
    const std::vector<glm::uvec4> cubeIndices{
        {0, 2, 4, 1}, // front
        {1, 4, 7, 5}, // right
        {5, 7, 6, 3}, // back
        {0, 3, 6, 2}, // left
        {2, 6, 7, 4}, // top
        {0, 1, 5, 3}  // bottom
    };

    for (size_t i = 0; i < primitiveCount; ++i) {
      std::copy(cubeVertices.begin(),
          cubeVertices.end(),
          std::back_insert_iterator(vertices));

      std::vector<glm::uvec4> newIndices = cubeIndices;

      for (auto &indexVector : newIndices) {
        indexVector += glm::uvec4(static_cast<int>(i * cubeVertices.size()));
      }

      std::copy(newIndices.begin(),
          newIndices.end(),
          std::back_insert_iterator(indices));
    }

    // add random transform per cube
    for (size_t k = 0; k < primitiveCount; ++k) {
      float cubeScale = getRandom(0.0f, 0.4f);

      const glm::mat4 scale = glm::scale(
          glm::mat4(1.0f), glm::vec3(cubeScale, cubeScale, cubeScale));

      const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f),
          getRandom(0.0f, 360.0f),
          glm::vec3(getRandom(0.0f, 1.0f),
              getRandom(0.0f, 1.0f),
              getRandom(0.0f, 1.0f)));

      const glm::mat4 translation = glm::translate(glm::mat4(1.0f),
          glm::vec3(getRandom(0.0f, 0.6f),
              getRandom(0.0f, 0.6f),
              getRandom(0.0f, 0.6f)));

      const glm::mat4 transform = translation * rotation * scale;

      for (size_t i = 0; i < cubeVertices.size(); ++i) {
        const size_t index = i + cubeVertices.size() * k;
        vertices[index] = transform * glm::vec4(vertices[index], 1.0);
      }
    }

    return std::make_tuple(vertices, indices);
  }

std::tuple<std::vector<glm::vec3>, std::vector<float>> PrimitiveGenerator::generateSpheres(
  size_t primitiveCount)
  {
    std::vector<glm::vec3> vertices;
    std::vector<float> radii;

    for (size_t i = 0; i < primitiveCount; ++i) {
      vertices.push_back(glm::vec3(
          getRandom(0.0f, 1.0f), getRandom(0.0f, 1.0f), getRandom(0.0f, 1.0f)));
      radii.push_back(getRandom(0.0f, 0.4f));
    }

    return std::make_tuple(vertices, radii);
  }

std::tuple<std::vector<glm::vec3>, std::vector<float>>
  PrimitiveGenerator::generateCones(size_t primitiveCount)
  {
    std::vector<glm::vec3> vertices;
    std::vector<float> radii;

    for (size_t i = 0; i < primitiveCount * 2; ++i) {
      vertices.push_back(glm::vec3(
          getRandom(0.0f, 1.0f), getRandom(0.0f, 1.0f), getRandom(0.0f, 1.0f)));
      radii.push_back(getRandom(0.0f, 0.4f));
    }

    // add translation offset per cone
    for (size_t i = 0; i < primitiveCount; ++i) {
      const size_t index = i * 2;
      const glm::vec3 offset(
          getRandom(0.0f, 0.6f), getRandom(0.0f, 0.6f), getRandom(0.0f, 0.6f));
      vertices[index] = (vertices[index] * 0.4f) + offset;
      vertices[index + 1] = (vertices[index + 1] * 0.4f) + offset;
    }

    return std::make_tuple(vertices, radii);
  }

std::tuple<std::vector<glm::vec3>, std::vector<float>>
  PrimitiveGenerator::generateCylinders(size_t primitiveCount)
  {
    std::vector<glm::vec3> vertices;
    std::vector<float> radii;

    for (size_t i = 0; i < primitiveCount; ++i) {
      vertices.push_back(glm::vec3(
          getRandom(0.0f, 1.0f), getRandom(0.0f, 1.0f), getRandom(0.0f, 1.0f)));
      vertices.push_back(glm::vec3(
          getRandom(0.0f, 1.0f), getRandom(0.0f, 1.0f), getRandom(0.0f, 1.0f)));
      radii.push_back(getRandom(0.0f, 0.4f));
    }

    // add translation offset per cylinder
    for (size_t i = 0; i < primitiveCount; ++i) {
      const size_t index = i * 2;
      const glm::vec3 offset(
          getRandom(0.0f, 0.6f), getRandom(0.0f, 0.6f), getRandom(0.0f, 0.6f));
      vertices[index] = (vertices[index] * 0.4f) + offset;
      vertices[index + 1] = (vertices[index + 1] * 0.4f) + offset;
    }

    return std::make_tuple(vertices, radii);
  }
} // namespace cts
