

// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SceneGenerator.h"
#include "anariWrapper.h"

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
      {"primitveMode", ANARI_STRING, "soup", "How the data is arranged (soup or indexed)"},
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
  std::string primitveMode = getParam<std::string>("primitveMode", "soup");
  size_t primitiveCount = getParam<size_t>("primitiveCount", 20);
  std::string shape = getParam<std::string>("shape", "triangle");
  unsigned int seed = getParam<unsigned int>("seed", 0);

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

  m_rng.seed(seed);

  std::vector<glm::vec3> vertices;
  if (geometrySubtype == "triangle") {
    if (shape == "triangle") {
      vertices = generateTriangles(primitiveCount);
    } else if (shape == "quad") {
      vertices = generateTriangulatedQuads(primitiveCount);
    } else if (shape == "cube") {
      vertices = generateTriangulatedCubes(primitiveCount);
    }
  } else if (geometrySubtype == "quad") {
    if (shape == "quad") {
      //TODO
    } else if (shape == "cube") {
      // TODO
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

std::vector<glm::vec3> SceneGenerator::generateTriangles(size_t primitiveCount)
{
  std::vector<glm::vec3> vertices((primitiveCount * 3));
  for (auto& vertex : vertices) {
    vertex.x = getRandom(0.0f, 1.0f);
    vertex.y = getRandom(0.0f, 1.0f);
    vertex.z = getRandom(0.0f, 1.0f);
  }

  // add offset per triangle
  for (size_t i = 0; i < vertices.size() - 2; i += 3) {
    glm::vec3 offset(getRandom(0.0f, 0.6f), getRandom(0.0f, 0.6f), getRandom(0.0f, 0.6f));
    vertices[i] = (vertices[i] * 0.4f) + offset;
    vertices[i + 1] = (vertices[i + 1] * 0.4f) + offset;
    vertices[i + 2] = (vertices[i + 2] * 0.4f) + offset;
  }

  return vertices;
}

std::vector<glm::vec3> SceneGenerator::generateTriangulatedQuads(size_t primitiveCount)
{
  // TODO also create indexed quads
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
      const glm::vec3 side0 = vertex1 - vertex0;
      const glm::vec3 side1 = vertex2 - vertex0;
      const glm::vec3 normal = glm::normalize(glm::cross(side0, side1));

      const glm::vec3 normalProjection =
          (glm::dot(vertex - vertex0, normal) / glm::dot(normal, normal)) * normal;
      vertex = vertex - normalProjection;
    }

    i = (i + 1) % 6;
  }

  // add offset per quad
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

std::vector<glm::vec3> SceneGenerator::generateTriangulatedCubes(size_t primitiveCount)
{
  std::vector<glm::vec3> vertices{{0.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, {1.0, 0.0, 0.0}};

  // TODO create random cubes
  return vertices;
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

  auto pixels = anari::map<float>(m_device, frame, "depth").data;

  std::vector<uint32_t> converted;
  for (int i = 0; i < image_height * image_width; ++i) {
    uint8_t colorValue = pixels[i] * 255;
    uint32_t rgba =
        (255 << 24) + (colorValue << 16) + (colorValue << 8) + colorValue;
    converted.push_back(rgba);
  }

  result.emplace_back(converted);

  anari::unmap(m_device, frame, "depth");

  anari::release(m_device, camera);
  anari::release(m_device, frame);
  anari::release(m_device, renderer);

  resetAllParameters();

  return result;
}

void SceneGenerator::resetAllParameters() {
  for (auto param : parameters()) {
    removeParam(param.name);
  }
}

float SceneGenerator::getRandom(float min, float max)
{
  std::uniform_real_distribution<float> uniformDist(min, max);

  return uniformDist(m_rng);
}

SceneGenerator *SceneGenerator::createSceneGenerator(const std::string &library,
  const std::string& device,
  const std::function<void(const std::string message)>& callback)
{
  m_library = anari::loadLibrary(library.c_str(), statusFunc, &callback);
  if (m_library == nullptr) {
    callback("library could not be loaded: " + library);
    return nullptr;
  }

  ANARIDevice dev = anariNewDevice(m_library, device.c_str());
  if(dev == nullptr) {
    callback("device could not be created: " + device);
    return nullptr;
  }

  return new SceneGenerator(dev);
}

} // namespace cts