// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "PrimitiveGenerator.h"

#include <glm/gtc/matrix_transform.hpp>
#include <iterator>
#include <vector>

namespace cts {

PrimitiveGenerator::PrimitiveGenerator(int seed)
{
  m_rng.seed(seed);
}

// helper function to get random float in range min..max
float PrimitiveGenerator::getRandomFloat(float min, float max)
{
  auto randomNumber = m_rng();
  float scaledNumber = randomNumber / (m_rng.max() / (max - min)) + min;
  return scaledNumber;
}

// helper function to get random vec3 with each component in range min..max
glm::vec3 PrimitiveGenerator::getRandomVector3(float min, float max)
{
  return {getRandomFloat(min, max),
      getRandomFloat(min, max),
      getRandomFloat(min, max)};
}

// helper function to get random vec4 with each component in range min..max
glm::vec4 PrimitiveGenerator::getRandomVector4(float min, float max)
{
  return {getRandomFloat(min, max),
      getRandomFloat(min, max),
      getRandomFloat(min, max),
      getRandomFloat(min, max)};
}

// applies random translation on a per primitives basis to create more useful test scenes
// keeping the vertices roughly in range 0..1
std::vector<glm::vec3> PrimitiveGenerator::randomTranslate(
    std::vector<glm::vec3> vertices, size_t verticesPerPrimitive)
{
  const float maxTranslation = 0.6f;
  const float scale = 0.4f;

  for (size_t i = 0; i < vertices.size();) {
    const glm::vec3 translation(getRandomVector3(0.0f, maxTranslation));
    for (size_t k = 0; k < verticesPerPrimitive; ++k, ++i) {
      vertices[i] = (vertices[i] * scale) + translation;
    }
  }

  return vertices;
}


// applies random transformation on a per primitive basis to create more useful test scenes
// keeping the vertices roughly in range 0..1
std::vector<glm::vec3> PrimitiveGenerator::randomTransform(
    std::vector<glm::vec3> vertices, size_t verticesPerPrimitive)
{
  const size_t primitiveCount = vertices.size() / verticesPerPrimitive;

  // generate a randomized transform for each primitive
  for (size_t k = 0; k < primitiveCount; ++k) {
    const glm::mat4 scale =
        glm::scale(glm::mat4(1.0f), glm::vec3(getRandomFloat(0.0f, 0.4f)));

    const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f),
        getRandomFloat(0.0f, 360.0f),
        getRandomVector3(0.0f, 1.0f));

    const glm::mat4 translation =
        glm::translate(glm::mat4(1.0f), getRandomVector3(0.0f, 0.6f));

    const glm::mat4 transform = translation * rotation * scale;

    // apply one randomized transform to all vertices that belong to the same primitive
    for (size_t i = 0; i < verticesPerPrimitive; ++i) {
      const size_t index = i + verticesPerPrimitive * k;
      vertices[index] = transform * glm::vec4(vertices[index], 1.0);
    }
  }

  return vertices;
}

// returns vector of randomized attribute data, each attribute being in range min..max
std::vector<glm::vec4> PrimitiveGenerator::generateAttribute(size_t elementCount, float min, float max) {
  std::vector<glm::vec4> attributes(elementCount);
  for (auto &attribute : attributes) {
    attribute = getRandomVector4(min, max);
  }
  return attributes;
}

// returns vector of vertices, each set of 3 vertices defines a randomly oriented triangle
// roughly in range 0..1
std::vector<glm::vec3> PrimitiveGenerator::generateTriangles(
    size_t primitiveCount)
{
  std::vector<glm::vec3> vertices(primitiveCount * 3);
  for (auto& vertex : vertices) {
    vertex = getRandomVector3(0.0f, 1.0f);
  }

  vertices = randomTranslate(vertices, 3);

  return vertices;
}

// returns soup vector of vertices, each set of 6 vertices defines a randomly oriented quad
// consisting of 2 triangles. Range is roughly 0..1
std::vector<glm::vec3> PrimitiveGenerator::generateTriangulatedQuadsSoup(
    size_t primitiveCount)
{
  std::vector<glm::vec3> vertices(primitiveCount * 6);
  
  // create quads (all 6 vertices of any quad lie in a plane)
  size_t i = 0;
  glm::vec3 vertex0(0), vertex1(0), vertex2(0);
  for (auto& vertex : vertices) {
    if (i == 3) {
      vertex = vertex2;
    } else if (i == 4) {
      vertex = vertex1;
    } else if (i == 5) {
      vertex = vertex2 + (vertex1 - vertex0);
    } else {
      vertex = getRandomVector3(0.0f, 1.0f);

      if (i == 0) {
        vertex0 = vertex;
      } else if (i == 1) {
        vertex1 = vertex;
      } else if (i == 2) {
        vertex2 = vertex;
      }
    }

    i = (i + 1) % 6;
  }

  vertices = randomTranslate(vertices, 6);

  return vertices;
}

// returns vectors of vertices and indices, each set of 4 vertices defines a randomly oriented quad
// consisting of 2 triangles, defined by the indices. Range is roughly 0..1
std::tuple<std::vector<glm::vec3>, std::vector<glm::uvec3>>
PrimitiveGenerator::generateTriangulatedQuadsIndexed(size_t primitiveCount)
{
  std::vector<glm::vec3> vertices(primitiveCount * 4);
  std::vector<glm::uvec3> indices(primitiveCount * 2);

  // create quads (all 4 vertices of any quad lie in a plane)
  size_t i = 0;
  glm::vec3 vertex0(0), vertex1(0), vertex2(0);
  for (auto &vertex : vertices) {
    if (i == 3) {
      vertex = vertex2 + (vertex1 - vertex0);
    } else {
      vertex = getRandomVector3(0.0f, 1.0f);

      if (i == 0) {
        vertex0 = vertex;
      } else if (i == 1) {
        vertex1 = vertex;
      } else if (i == 2) {
        vertex2 = vertex;
      }
    }

    i = (i + 1) % 4;
  }

  vertices = randomTranslate(vertices, 4);

  // create indices (set of 3 per triangle, 6 per quad)
  for (size_t k = 0; k < primitiveCount; ++k) {
    const size_t index = k * 4;
    const size_t indicesIndex = k * 2;
    indices[indicesIndex] = glm::uvec3(index, index + 1, index + 2);
    indices[indicesIndex + 1] = glm::uvec3(index + 2, index + 1, index + 3);
  }

  return std::make_tuple(vertices, indices);
}

// returns soup vector of vertices, each set of 36 vertices defines a randomly transformed cube
// consisting of 12 triangles. Range is roughly 0..1
std::vector<glm::vec3> PrimitiveGenerator::generateTriangulatedCubesSoup(
    size_t primitiveCount)
{
  std::vector<glm::vec3> vertices;
  // vertices of all triangles of a basic cube, used as a basis for each primitive
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

  vertices = randomTransform(vertices, cubeVertices.size());

  return vertices;
}

// returns vectors of vertices and indices, each set of 8 vertices defines a randomly transformed cube
// consisting of 12 triangles, defined by the indices. Range is roughly 0..1
std::tuple<std::vector<glm::vec3>, std::vector<glm::uvec3>> PrimitiveGenerator::generateTriangulatedCubesIndexed(
    size_t primitiveCount)
{
  std::vector<glm::vec3> vertices;
  std::vector<glm::uvec3> indices;
  // vertices of a basic cube, used as a basis for each primitive
  const std::vector<glm::vec3> cubeVertices {
    {0.0, 0.0, 0.0}, 
    {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, 
    {1.0, 1.0, 0.0}, {1.0, 0.0, 1.0}, {0.0, 1.0, 1.0}, 
    {1.0, 1.0, 1.0}};
  // triangle indices of a basic cube, used as a basis for each primitive
  const std::vector<glm::uvec3> cubeIndices {
    {0, 2, 1}, {1, 2, 4}, // front
    {1, 4, 5}, {5, 4, 7}, // right
    {5, 7, 3}, {6, 7, 3}, // back
    {0, 3, 6}, {0, 6, 2}, // left
    {2, 7, 4}, {2, 6, 7}, // top
    {0, 5, 1}, {0, 3, 5}  // bottom
  };

  // fill vertex and index vectors per primitive
  for (size_t i = 0; i < primitiveCount; ++i) {
    std::copy(cubeVertices.begin(),
        cubeVertices.end(),
        std::back_insert_iterator(vertices));

    std::vector<glm::uvec3> newIndices = cubeIndices;

    // add offset to basic cube indices depending on number of primitives
    for (auto &indexVector : newIndices) {
      indexVector += glm::uvec3(static_cast<int>(i * cubeVertices.size()));
    }

    std::copy(newIndices.begin(),
        newIndices.end(),
        std::back_insert_iterator(indices));
  }

  vertices = randomTransform(vertices, cubeVertices.size());

  return std::make_tuple(vertices, indices);
}

// returns vector of vertices, each set of 4 vertices defines a randomly oriented quad
// roughly in range 0..1
std::vector<glm::vec3> PrimitiveGenerator::generateQuads(size_t primitiveCount)
{
  std::vector<glm::vec3> vertices;

  for (size_t i = 0; i < primitiveCount; ++i) {
    glm::vec3 vertex0(0), vertex1(0), vertex2(0), vertex3(0);

    vertex0 = getRandomVector3(0.0f, 1.0f);
    vertex1 = getRandomVector3(0.0f, 1.0f);
    vertex2 = getRandomVector3(0.0f, 1.0f);
    vertex3 = vertex2 + (vertex1 - vertex0);

    std::vector<glm::vec3> quad = {vertex0, vertex1, vertex3, vertex2};

    std::copy(quad.begin(), quad.end(), std::back_insert_iterator(vertices));
  }

  vertices = randomTranslate(vertices, 4);

  return vertices;
}

// returns soup vector of vertices, each set of 24 vertices defines a randomly transformed cube
// consisting of 6 quads. Range is roughly 0..1
std::vector<glm::vec3> PrimitiveGenerator::generateQuadCubesSoup(
      size_t primitiveCount) 
  {
  std::vector<glm::vec3> vertices;
  // vertices of all quads of a basic cube, used as a basis for each primitive
  const std::vector<glm::vec3> cubeVertices{
    {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, {0.0, 1.0, 0.0}, // front
    {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, {1.0, 1.0, 1.0}, {1.0, 0.0, 1.0}, // right
    {1.0, 0.0, 1.0}, {0.0, 0.0, 1.0}, {0.0, 1.0, 1.0}, {1.0, 1.0, 1.0}, // back
    {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 1.0, 1.0}, {0.0, 0.0, 1.0}, // left
    {0.0, 1.0, 0.0}, {1.0, 1.0, 0.0}, {1.0, 1.0, 1.0}, {0.0, 1.0, 1.0}, // top
    {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {1.0, 0.0, 1.0}, {1.0, 0.0, 0.0}};// bottom

  for (size_t i = 0; i < primitiveCount; ++i) {
    std::copy(cubeVertices.begin(),
        cubeVertices.end(),
        std::back_insert_iterator(vertices));
  }

  vertices = randomTransform(vertices, cubeVertices.size());

  return vertices;
}

// returns vectors of vertices and indices, each set of 8 vertices defines a randomly transformed cube
// consisting of 6 quads, defined by the indices. Range is roughly 0..1
std::tuple<std::vector<glm::vec3>, std::vector<glm::uvec4>>
PrimitiveGenerator::generateQuadCubesIndexed(size_t primitiveCount)
{
  std::vector<glm::vec3> vertices;
  std::vector<glm::uvec4> indices;
  // vertices of a basic cube, used as a basis for each primitive
  const std::vector<glm::vec3> cubeVertices{{0.0, 0.0, 0.0},
      {1.0, 0.0, 0.0},
      {0.0, 1.0, 0.0},
      {0.0, 0.0, 1.0},
      {1.0, 1.0, 0.0},
      {1.0, 0.0, 1.0},
      {0.0, 1.0, 1.0},
      {1.0, 1.0, 1.0}};
  // quad indices of a basic cube, used as a basis for each primitive
  const std::vector<glm::uvec4> cubeIndices{
      {0, 2, 4, 1}, // front
      {1, 4, 7, 5}, // right
      {5, 7, 6, 3}, // back
      {0, 3, 6, 2}, // left
      {2, 6, 7, 4}, // top
      {0, 1, 5, 3} // bottom
  };

  // fill vertex and index vectors per primitive
  for (size_t i = 0; i < primitiveCount; ++i) {
    std::copy(cubeVertices.begin(),
        cubeVertices.end(),
        std::back_insert_iterator(vertices));

    std::vector<glm::uvec4> newIndices = cubeIndices;

    // add offset to basic cube indices depending on number of primitives
    for (auto &indexVector : newIndices) {
      indexVector += glm::uvec4(static_cast<int>(i * cubeVertices.size()));
    }

    std::copy(newIndices.begin(),
        newIndices.end(),
        std::back_insert_iterator(indices));
  }

  vertices = randomTransform(vertices, cubeVertices.size());

  return std::make_tuple(vertices, indices);
}

// returns vectors of vertices and radii
// each set of 1 vertex and 1 radius defines a randomly transformed sphere
// roughly in range 0..1
std::tuple<std::vector<glm::vec3>, std::vector<float>>
PrimitiveGenerator::generateSpheres(size_t primitiveCount)
{
  std::vector<glm::vec3> vertices;
  std::vector<float> radii;

  for (size_t i = 0; i < primitiveCount; ++i) {
    vertices.push_back(getRandomVector3(0.0f, 1.0f));
    radii.push_back(getRandomFloat(0.0f, 0.4f));
  }

  return std::make_tuple(vertices, radii);
}

// returns vectors of vertices and radii
// each set of 2 vertices and 2 radii defines a randomly transformed curve segment
// roughly in range 0..1
std::tuple<std::vector<glm::vec3>, std::vector<float>>
PrimitiveGenerator::generateCurves(size_t primitiveCount)
{
  std::vector<glm::vec3> vertices;
  std::vector<float> radii;

  for (size_t i = 0; i < primitiveCount * 2; ++i) {
    vertices.push_back(getRandomVector3(0.0f, 1.0f));
    radii.push_back(getRandomFloat(0.001f, 0.05f));
  }

  vertices = randomTranslate(vertices, 2);

  return std::make_tuple(vertices, radii);
}

// returns vectors of vertices and radii
// each set of 2 vertices and 2 radii defines a randomly transformed cone
// roughly in range 0..1
std::tuple<std::vector<glm::vec3>, std::vector<float>, std::vector<uint8_t>>
PrimitiveGenerator::generateCones(
    size_t primitiveCount, std::optional<int32_t> vertexCaps)
{
  std::vector<glm::vec3> vertices;
  std::vector<float> radii;
  std::vector<uint8_t> caps;

  for (size_t i = 0; i < primitiveCount * 2; ++i) {
    vertices.push_back(getRandomVector3(0.0f, 1.0f));
    radii.push_back(getRandomFloat(0.0f, 0.4f));
    if (vertexCaps.has_value()) {
      caps.push_back(vertexCaps.value() == 0 ? 0u : 1u);
    }
  }

  vertices = randomTranslate(vertices, 2);

  return std::make_tuple(vertices, radii, caps);
}

// returns vectors of vertices and radii
// each set of 2 vertices and 1 radius defines a randomly transformed cylinder
// roughly in range 0..1
std::tuple<std::vector<glm::vec3>, std::vector<float>, std::vector<uint8_t>>
PrimitiveGenerator::generateCylinders(
    size_t primitiveCount, std::optional<int32_t> vertexCaps)
{
  std::vector<glm::vec3> vertices;
  std::vector<float> radii;
  std::vector<uint8_t> caps;

  for (size_t i = 0; i < primitiveCount; ++i) {
    vertices.push_back(getRandomVector3(0.0f, 1.0f));
    vertices.push_back(getRandomVector3(0.0f, 1.0f));
    radii.push_back(getRandomFloat(0.0f, 0.4f));
    if (vertexCaps.has_value()) {
      caps.push_back(vertexCaps.value() == 0 ? 0u : 1u);
    }
  }

  vertices = randomTranslate(vertices, 2);

  return std::make_tuple(vertices, radii, caps);
}
} // namespace cts
