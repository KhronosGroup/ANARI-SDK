

// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SceneGenerator.h"
#include "PrimitiveGenerator.h"
#include "anariWrapper.h"

#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>


namespace cts {


SceneGenerator::SceneGenerator(
    anari::Device device)
    : TestScene(device)
{
  //anari::commitParameters(m_device, m_device);
  m_world = anari::newObject<anari::World>(m_device);
}

SceneGenerator::~SceneGenerator()
{
  for (auto& [key, value] : m_anariObjects) {
    for (auto &object : value) {
      anari::release(m_device, object);
    }
  }
  anari::release(m_device, m_world);
}

anari::World SceneGenerator::world()
{
  return m_world;
}

void SceneGenerator::commit()
{
  auto d = m_device;

  std::string geometrySubtype = getParam<std::string>("geometrySubtype", "triangle");
  std::string primitiveMode = getParam<std::string>("primitiveMode", "soup");
  int primitiveCount = getParam<int>("primitiveCount", 20);
  std::string shape = getParam<std::string>("shape", "triangle");
  int seed = getParam<int>("seed", 0);

  // Build this scene top-down to stress commit ordering guarantees

  setDefaultLight(m_world);

  auto surface = anari::newObject<anari::Surface>(d);
  auto geom = anari::newObject<anari::Geometry>(d, geometrySubtype.c_str());
  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", "color");
  anari::commitParameters(d, mat);

  anari::setAndReleaseParameter(
      d, m_world, "surface", anari::newArray1D(d, &surface));

  anari::commitParameters(d, m_world);

  anari::setParameter(d, surface, "geometry", geom);
  anari::setParameter(d, surface, "material", mat);

  PrimitiveGenerator generator(seed);

  std::vector<glm::vec3> vertices;
  if (geometrySubtype == "triangle") {
    std::vector<glm::uvec3> indices;
    if (shape == "triangle") {
      vertices = generator.generateTriangles(primitiveCount);

      if (primitiveMode == "indexed") {
        for(size_t i = 0; i < vertices.size(); i +=3) {
          indices.push_back(glm::uvec3(i, i + 1, i + 2));
        }
      }
    } else if (shape == "quad") {
      if (primitiveMode == "indexed") {
       auto [quadVertices, quadIndices] =
           generator.generateTriangulatedQuadsIndexed(primitiveCount);
       vertices = quadVertices;
       indices = quadIndices;
      } else {
       vertices = generator.generateTriangulatedQuadsSoup(primitiveCount);
      }
    } else if (shape == "cube") {
      if (primitiveMode == "indexed") {
       auto [cubeVertices, cubeIndices] =
           generator.generateTriangulatedCubesIndexed(primitiveCount);
       vertices = cubeVertices;
       indices = cubeIndices;
      } else {
       vertices = generator.generateTriangulatedCubesSoup(primitiveCount);
      }
    }

    if (primitiveMode == "indexed") {
    // shuffle indices vector to create a more useful test case
      indices = generator.shuffleVector(indices);
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  } else if (geometrySubtype == "quad") {
    std::vector<glm::uvec4> indices;
    if (shape == "quad") {
      vertices = generator.generateQuads(primitiveCount);

      if (primitiveMode == "indexed") {
        for (size_t i = 0; i < vertices.size(); i += 4) {
          indices.push_back(glm::uvec4(i, i + 1, i + 2, i + 3));
        }
      }
    } else if (shape == "cube") {
      if (primitiveMode == "indexed") {
        auto [cubeVertices, cubeIndices] =
            generator.generateQuadCubesIndexed(primitiveCount);
        vertices = cubeVertices;
        indices = cubeIndices;
      } else {
        vertices = generator.generateQuadCubesSoup(primitiveCount);
      }
    }

    if (primitiveMode == "indexed") {
    // shuffle indices vector to create a more useful test case
      indices = generator.shuffleVector(indices);
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  } else if (geometrySubtype == "sphere") {
    auto [sphereVertices, sphereRadii] =
        generator.generateSpheres(primitiveCount);
    vertices = sphereVertices;

    anari::setAndReleaseParameter(d,
        geom,
        "vertex.radius",
        anari::newArray1D(d, sphereRadii.data(), sphereRadii.size()));

    if ("indexed") {
      std::vector<uint32_t> indices;
      for (size_t i = 0; i < vertices.size(); ++i) {
        indices.push_back(static_cast<uint32_t>(i));
      }

      // shuffle indices vector to create a more useful test case
      indices = generator.shuffleVector(indices);
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  } else if (geometrySubtype == "curve") {
    auto [curveVertices, curveRadii] = generator.generateCurves(primitiveCount);
    vertices = curveVertices;

    anari::setAndReleaseParameter(d,
        geom,
        "vertex.radius",
        anari::newArray1D(d, curveRadii.data(), curveRadii.size()));

    if ("indexed") {
      std::vector<glm::uvec2> indices;
      for (size_t i = 0; i < vertices.size(); i += 2) {
        indices.push_back(
            glm::vec2(static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1)));
      }

      // shuffle indices vector to create a more useful test case
      indices = generator.shuffleVector(indices);
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  } else if (geometrySubtype == "cone") {
    auto [coneVertices, coneRadii] =
        generator.generateCones(primitiveCount);
    vertices = coneVertices;

    anari::setAndReleaseParameter(d,
        geom,
        "vertex.radius",
        anari::newArray1D(d, coneRadii.data(), coneRadii.size()));

    if ("indexed") {
      std::vector<glm::uvec2> indices;
      for (size_t i = 0; i < vertices.size(); i += 2) {
        indices.push_back(
            glm::vec2(static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1)));
      }

      // shuffle indices vector to create a more useful test case
      indices = generator.shuffleVector(indices);
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  } else if (geometrySubtype == "cylinder") {
    auto [cylinderVertices, cylinderRadii] = generator.generateCylinders(primitiveCount);
    vertices = cylinderVertices;

    anari::setAndReleaseParameter(d,
        geom,
        "primitive.radius",
        anari::newArray1D(d, cylinderRadii.data(), cylinderRadii.size()));

    if ("indexed") {
      std::vector<glm::uvec2> indices;
      for (size_t i = 0; i < vertices.size(); i += 2) {
        indices.push_back(glm::vec2(
            static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1)));
      }

      // shuffle indices vector to create a more useful test case
      indices = generator.shuffleVector(indices);
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  }

  anari::setAndReleaseParameter(d,
      geom,
      "vertex.position",
      anari::newArray1D(d, vertices.data(), vertices.size()));


  anari::commitParameters(d, geom);
  anari::commitParameters(d, mat);
  anari::commitParameters(d, surface);

  // cleanup

  anari::release(d, surface);
  anari::release(d, geom);
  anari::release(d, mat);
}

std::vector<std::vector<uint32_t>> SceneGenerator::renderScene(
    const std::string &rendererType, float renderDistance)
{
  size_t image_height = getParam<size_t>("image_height", 1024);
  size_t image_width = getParam<size_t>("image_width", 1024);
  std::string color_type_param = getParam<std::string>("frame_color_type", "");
  int componentBytes = 1;
  ANARIDataType color_type = ANARI_UNKNOWN;
  if (color_type_param == "UFIXED8_RGBA_SRGB") {
    color_type = ANARI_UFIXED8_RGBA_SRGB;
  } else if (color_type_param == "FLOAT32_VEC4") {
    color_type = ANARI_FLOAT32_VEC4;
    componentBytes = 4;
  } else if (color_type_param == "UFIXED8_VEC4") {
    color_type = ANARI_UFIXED8_VEC4;
  }

  std::string depth_type_param = getParam<std::string>("frame_depth_type", "");

  auto camera = anari::newObject<anari::Camera>(m_device, "perspective");
  anari::setParameter(
      m_device, camera, "aspect", (float)image_height / (float)image_width);

  auto renderer =
      anari::newObject<anari::Renderer>(m_device, rendererType.c_str());
  //anari::setParameter(d, renderer, "pixelSamples", g_numPixelSamples);
  //anari::setParameter(m_device, renderer, "backgroundColor", glm::vec4(glm::vec3(0.1), 1));
  anari::commitParameters(m_device, renderer);

  auto frame = anari::newObject<anari::Frame>(m_device);
  anari::setParameter(m_device, frame, "size", glm::uvec2(image_height, image_width));
  if (color_type != ANARI_UNKNOWN) {
    anari::setParameter(m_device, frame, "color", color_type);
  }
  if (depth_type_param == "FLOAT32") {
    anari::setParameter(m_device, frame, "depth", ANARI_FLOAT32);
  }

  anari::setParameter(m_device, frame, "renderer", renderer);
  anari::setParameter(m_device, frame, "camera", camera);
  anari::setParameter(m_device, frame, "world", m_world);

  anari::commitParameters(m_device, frame);

  auto cam = createDefaultCameraFromWorld(m_world);
  anari::setParameter(m_device, camera, "position", cam.position);
  anari::setParameter(m_device, camera, "direction", cam.direction);
  anari::setParameter(m_device, camera, "up", cam.up);
  anari::commitParameters(m_device, camera);


  anari::render(m_device, frame);
  anari::wait(m_device, frame);

  std::vector<std::vector<uint32_t>> result;

  if (color_type != ANARI_UNKNOWN) {
    if (color_type == ANARI_FLOAT32_VEC4) {
      const float *pixels = anari::map<float>(m_device, frame, "color").data;
      std::vector<uint32_t> converted;
      for (int i = 0; i < image_height * image_width; ++i) {
        uint32_t rgba = 0;
        for (int j = 0; j < componentBytes; ++j) {
          uint8_t colorValue =
              static_cast<uint8_t>(pixels[i * componentBytes + j] * 255.0f);
          rgba += colorValue << (8 * j);
        }
        converted.push_back(rgba);
      }

      result.emplace_back(converted);
      anari::unmap(m_device, frame, "color");
    } else {
      auto fb = anari::map<uint32_t>(m_device, frame, "color");
      result.emplace_back(
          fb.data, fb.data + image_height * image_width);
      anari::unmap(m_device, frame, "color");
    }
  } else {
    result.emplace_back();
  }

  if (depth_type_param == "FLOAT32") {
    const float *pixels = anari::map<float>(m_device, frame, "depth").data;

    std::vector<uint32_t> converted;
    for (int i = 0; i < image_height * image_width; ++i) {
      uint8_t colorValue =
          static_cast<uint8_t>(pixels[i] / renderDistance * 255.0f);
      uint32_t rgba =
          (255 << 24) + (colorValue << 16) + (colorValue << 8) + colorValue;
      converted.push_back(rgba);
    }

    result.emplace_back(converted);

    anari::unmap(m_device, frame, "depth");
  } else {
    result.emplace_back();
  }

  if (!anariGetProperty(m_device,
      frame,
      "duration",
      ANARI_FLOAT32,
          &frameDuration,
          sizeof(frameDuration),
          ANARI_WAIT)) {
    frameDuration = -1.0f;
  }

  anari::release(m_device, camera);
  anari::release(m_device, frame);
  anari::release(m_device, renderer);

  return result;
}

void SceneGenerator::resetAllParameters() {
  for (auto &[key, value] : m_anariObjects) {
    for (auto &object : value) {
      anari::release(m_device, object);
    }
  }
  m_anariObjects.clear();

  anari::unsetParameter(m_device, m_world, "instance");
  anari::unsetParameter(m_device, m_world, "surface");
  anari::unsetParameter(m_device, m_world, "volume");
  anari::unsetParameter(m_device, m_world, "light");

  std::vector<std::string> paramNames;
  for (auto it = params_begin(); it != params_end(); ++it) {
    paramNames.push_back(it->get()->name);
  }
  for (auto &name : paramNames) {
    removeParam(name);
  }
}

std::vector<std::vector<std::vector<std::vector<float>>>> SceneGenerator::getBounds()
{
  std::vector<std::vector<std::vector<std::vector<float>>>> result;
  std::vector<std::vector<std::vector<float>>> worldBounds;
  std::vector<std::vector<std::vector<float>>> instancesBounds;
  std::vector<std::vector<std::vector<float>>> groupBounds;
  std::vector<std::vector<float>> &singleBound =
      worldBounds.emplace_back();
  auto anariWorldBounds = bounds();
  for (const auto &bound : anariWorldBounds) {
    std::vector<float>& vector = singleBound.emplace_back();
    for (int i = 0; i < bound.length(); ++i) {
      vector.push_back(bound[i]);
    }
  }
  if (m_anariObjects.find(int(ANARI_INSTANCE)) != m_anariObjects.end()) {
    for (auto &instance : m_anariObjects[int(ANARI_INSTANCE)]) {
      singleBound = instancesBounds.emplace_back();
      anari::scenes::box3 anariInstanceBounds;
      anari::getProperty(m_device, instance, "bounds", anariInstanceBounds);
      for (const auto &bound : anariInstanceBounds) {
        std::vector<float> &vector = singleBound.emplace_back();
        for (int i = 0; i < bound.length(); ++i) {
          vector.push_back(bound[i]);
        }
      }
    }
  }

  if (m_anariObjects.find(int(ANARI_GROUP)) != m_anariObjects.end()) {
    for (auto &group : m_anariObjects[int(ANARI_GROUP)]) {
      singleBound = groupBounds.emplace_back();
      anari::scenes::box3 anariGroupBounds;
      anari::getProperty(m_device, group, "bounds", anariGroupBounds);
      for (const auto &bound : anariGroupBounds) {
        std::vector<float> &vector = singleBound.emplace_back();
        for (int i = 0; i < bound.length(); ++i) {
          vector.push_back(bound[i]);
        }
      }
    }
  }
  result.emplace_back(worldBounds);
  result.emplace_back(instancesBounds);
  result.emplace_back(groupBounds);
  return result;
}

} // namespace cts