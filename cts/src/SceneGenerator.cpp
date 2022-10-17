

// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SceneGenerator.h"
#include "PrimitiveGenerator.h"
#include "anariWrapper.h"

#include <glm/gtc/matrix_transform.hpp>

namespace cts {

anari::Library SceneGenerator::m_library = nullptr;

SceneGenerator::SceneGenerator(anari::Device device) :  TestScene(device)
{
  //anari::commitParameters(m_device, m_device);
  m_world = anari::newObject<anari::World>(m_device);
}

SceneGenerator::~SceneGenerator()
{
  anari::release(m_device, m_world);
  anari::unloadLibrary(m_library);
}

std::vector<anari::scenes::ParameterInfo> SceneGenerator::parameters()
{
  return {
      {"geometrySubtype", ANARI_STRING, "triangle", "Which type of geometry to generate"},
      {"primitiveMode", ANARI_STRING, "soup", "How the data is arranged (soup or indexed)"},
      {"primitiveCount", ANARI_UINT32, 1, "How many primtives should be generated"},
      {"image_height",
          ANARI_UINT32,
          1024,
          "Height of the image"},
      {"image_width",
          ANARI_UINT32,
          1024,
          "Width of the image"},
      //
  };
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
       vertices = generator.generateTriangulatedQuadSoups(primitiveCount);
      }
    } else if (shape == "cube") {
      if (primitiveMode == "indexed") {
       auto [cubeVertices, cubeIndices] =
           generator.generateTriangulatedCubesIndexed(primitiveCount);
       vertices = cubeVertices;
       indices = cubeIndices;
      } else {
       vertices = generator.generateTriangulatedCubeSoups(primitiveCount);
      }
    }

    if (primitiveMode == "indexed") {
    // reverse indices vector to make a more useful test case
      std::reverse(indices.begin(), indices.end());
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
        generator.generateQuadCubeSoups(primitiveCount);
      }
    }

    if (primitiveMode == "indexed") {
      // reverse indices vector to make a more useful test case
      std::reverse(indices.begin(), indices.end());
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  } else if (geometrySubtype == "sphere") {
    auto [sphereVertices, sphereRadii] =
        generator.generateSphereSoup(primitiveCount);
    vertices = sphereVertices;

    anari::setAndReleaseParameter(d,
        geom,
        "vertex.radius",
        anari::newArray1D(d, sphereRadii.data(), sphereRadii.size()));
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

std::vector<std::vector<uint32_t>> SceneGenerator::renderScene(const std::string &rendererType)
{
  size_t image_height = getParam<size_t>("image_height", 1024);
  size_t image_width = getParam<size_t>("image_width", 1024);
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
  anari::setParameter(m_device, frame, "color", ANARI_UFIXED8_RGBA_SRGB);
  anari::setParameter(m_device, frame, "depth", ANARI_FLOAT32);

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

  auto fb = anari::map<uint32_t>(m_device, frame, "color");
  result.emplace_back(fb.data, fb.data + image_height * image_width);

  anari::unmap(m_device, frame, "color");

  const float* pixels = anari::map<float>(m_device, frame, "depth").data;

  std::vector<uint32_t> converted;
  for (int i = 0; i < image_height * image_width; ++i) {
    uint8_t colorValue = static_cast<uint8_t>(pixels[i] * 255.0f);
    uint32_t rgba =
        (255 << 24) + (colorValue << 16) + (colorValue << 8) + colorValue;
    converted.push_back(rgba);
  }

  result.emplace_back(converted);

  anari::unmap(m_device, frame, "depth");

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
  for (auto param : parameters()) {
    removeParam(param.name);
  }
}

std::vector<std::vector<float>> SceneGenerator::getBounds()
{
  std::vector<std::vector<float>> result;
  auto b = bounds();
  for (const auto& bound : b) {
    std::vector<float> vector;
    for (int i = 0; i < bound.length(); ++i) {
      vector.push_back(bound[i]);
    }
    result.push_back(vector);
  }
  return result;
}

SceneGenerator *SceneGenerator::createSceneGenerator(const std::string &library,
  const std::optional<std::string>& device,
  const std::function<void(const std::string message)>& callback)
{
  m_library = anari::loadLibrary(library.c_str(), statusFunc, &callback);
  if (m_library == nullptr) {
    throw std::runtime_error("Library could not be loaded: " + library);
  }

  std::string deviceName;
  if (device.has_value()) {
    deviceName = device.value();
  } else {
    const char **devices = anariGetDeviceSubtypes(m_library);
    if (!devices) {
      throw std::runtime_error("No device available");
    }
    deviceName = *devices;
  }

  ANARIDevice dev = anariNewDevice(m_library, deviceName.c_str());
  if(dev == nullptr) {
    anari::unloadLibrary(m_library);
    throw std::runtime_error("Device could not be created: " + deviceName);
  }

  return new SceneGenerator(dev);
}

} // namespace cts