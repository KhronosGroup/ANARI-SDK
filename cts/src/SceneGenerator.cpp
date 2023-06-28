// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SceneGenerator.h"
#include "PrimitiveGenerator.h"
#include "ColorPalette.h"
#include "TextureGenerator.h"
#include "anariWrapper.h"

#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>

namespace cts {

SceneGenerator::SceneGenerator(
    anari::Device device)
    : TestScene(device)
{
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

/***
* This function is not used currently. It documents all available parameters of a test scene
***/
std::vector<anari::scenes::ParameterInfo> SceneGenerator::parameters()
{
  return {
      {"geometrySubtype",
          "triangle",
          "Which type of geometry to generate. Possible values: triangle, quad, sphere, curve, cone, cylinder"},
      {"shape",
          "triangle",
          "Which shape should be generated. Currently only relevant for triangles and quads. Possible values: triangle, quad, cube"},
      {"primitiveMode",
          "soup",
          "How the data is arranged (soup or indexed)"},
      {"primitiveCount",
          1,
          "How many primitives should be generated"},
      {"frame_color_type",
          "",
          "Type of the color framebuffer. If empty, color buffer will not be used. Possible values: UFIXED8_RGBA_SRGB, FLOAT32_VEC4, UFIXED8_VEC4"},
      {"frame_depth_type",
          "",
          "Type of the depth framebuffer. If empty, depth buffer will not be used. Possible values: FLOAT32"},
      {"image_height", 1024, "Height of the image"},
      {"image_width", 1024, "Width of the image"},
      {"attribute_min", 0.0f, "Minimum random value for attributes"},
      {"attribute_max", 1.0f, "Maximum random value for attributes"},
      {"primitive_attributes", false, "If primitive attributes should be filled randomly"},
      {"vertex_attributes", false, "If vertex attributes should be filled randomly"},
      {"seed", 0u, "Seed for random number generator to ensure that tests are consistent across platforms"},
      {"camera_generate_transform", true, "If the camera position should be computed via world bounds"},
      {"vertexCaps", false, "Should cones and cylinders have caps (per vertex setting)"},
      {"globalCaps", "none", "Should cones and cylinders have caps (global setting). Possible values: \"none\", \"first\", \"second\", \"both\""},
      {"globalRadius", 1.0f, "Use the global radius property instead of a per vertex one"},
      {"unusedVertices", false, "The last primitive's indices in the index buffer will be removed to test handling of unused/skipped vertices in the vertex buffer"},
      {"color", "", "Fill an attribute with colors. Possible values: \"vertex.color\", \"vertex.attribute0\", \"primitive.attribute3\" and similar"},
      {"opacity", "", "Fill an attribute with opacity values. Possible values: \"vertex.attribute0\", \"primitive.attribute3\" and similar"}

      //
  };
}

int SceneGenerator::anariTypeFromString(const std::string& type) 
{
  if (type == "material") {
    return ANARI_MATERIAL;
  } 
  if (type == "camera") {
    return ANARI_CAMERA;
  } 
  if (type == "light") {
    return ANARI_LIGHT;
  }
  if (type == "sampler") {
    return ANARI_SAMPLER;
  }
  if (type == "geometry") {
    return ANARI_GEOMETRY;
  }
  if (type == "renderer") {
    return ANARI_RENDERER;
  }
  return ANARI_UNKNOWN;
}

void SceneGenerator::setReferenceParameter(int objectType, size_t objectIndex,
    const std::string &name, int refType, size_t refIndex)
{
  if (auto itObj = m_anariObjects.find(objectType);
      itObj != m_anariObjects.end() && objectIndex < itObj->second.size()) {
    auto object = itObj->second[objectIndex];
    if (m_device != nullptr) {
      if (auto itRef = m_anariObjects.find(refType);
          itRef != m_anariObjects.end() && refIndex < itRef->second.size()) {
        auto ref = itRef->second[refIndex];
        anari::setParameter(m_device, object, name.c_str(), ref);
      }
    }
  }
}

void SceneGenerator::setCurrentObject(int type, size_t index)
{
    if (auto it = m_anariObjects.find(type);
        it != m_anariObjects.end() && index < it->second.size()) {
        m_currentObject = it->second[index];
    }
}

void SceneGenerator::createAnariObject(
    int type, const std::string &subtype, std::string ctsType)
{
  ANARIObject object = nullptr;
  switch (type) {
  case ANARI_RENDERER: {
    object = anari::newObject<anari::Renderer>(m_device, subtype.c_str());
    auto it = m_anariObjects.try_emplace(
        int(ANARI_RENDERER), std::vector<ANARIObject>());
    if (it.first->second.size() == 0) {
      it.first->second.emplace_back(object);
    }
    break;
  }
  case ANARI_GEOMETRY: {
    object = anari::newObject<anari::Geometry>(m_device, subtype.c_str());
    auto it = m_anariObjects.try_emplace(
        int(ANARI_GEOMETRY), std::vector<ANARIObject>());
    if (it.first->second.size() == 0) {
      it.first->second.emplace_back(object);
    }
    break;
  }
  case ANARI_MATERIAL: {
    object = anari::newObject<anari::Material>(m_device, subtype.c_str());
    auto it = m_anariObjects.try_emplace(
        int(ANARI_MATERIAL), std::vector<ANARIObject>());
    if (it.first->second.size() == 0) {
      it.first->second.emplace_back(object);
    }
    break;
  }
  case ANARI_CAMERA: {
    object = anari::newObject<anari::Camera>(m_device, subtype.c_str());
    auto it = m_anariObjects.try_emplace(
        int(ANARI_CAMERA), std::vector<ANARIObject>());
    if (it.first->second.size() == 0) {
      it.first->second.emplace_back(object);
    }
    break;
  }
  case ANARI_LIGHT: {
    object = anari::newObject<anari::Light>(m_device, subtype.c_str());
    auto it = m_anariObjects.try_emplace(
        int(ANARI_LIGHT), std::vector<ANARIObject>());
    it.first->second.emplace_back(object);
    break;
  }
  case ANARI_SAMPLER: {
    object = anari::newObject<anari::Sampler>(m_device, subtype.c_str());
    auto it = m_anariObjects.try_emplace(
        int(ANARI_SAMPLER), std::vector<ANARIObject>());
    it.first->second.emplace_back(object);

    if (subtype == "image1D") {
      size_t resolution = 16;
      std::vector<glm::vec4> greyscale =
          TextureGenerator::generateGreyScale(resolution);
      anari::setAndReleaseParameter(m_device,
          object,
          "image",
          anari::newArray1D(m_device, greyscale.data(), resolution));
      break;
    }
    if (subtype == "image2D") {
      size_t resolution = 64;
      auto checkerboard = ctsType == "normal"
          ? TextureGenerator::generateCheckerBoardNormalMap(resolution)
          : TextureGenerator::generateCheckerBoard(resolution);
      anari::setAndReleaseParameter(m_device,
          object,
          "image",
          anari::newArray2D(m_device,
              checkerboard.data(), resolution, resolution));
      break;
    }
    if (subtype == "image3D") {
      size_t resolution = 32;
      auto rgbRamp = TextureGenerator::generateRGBRamp(resolution);
      anari::setAndReleaseParameter(m_device,
          object,
          "image",
          anari::newArray3D(m_device,
              rgbRamp.data(),
              resolution,
              resolution, resolution));
      break;
    }
    break;
  }
  }
  m_currentObject = object;
}

anari::World SceneGenerator::world()
{
  return m_world;
}

// generate the to be rendered scene
void SceneGenerator::commit()
{
  auto d = m_device;

  for (auto &items : m_anariObjects) {
    for (auto &object : items.second) {
      anari::commitParameters(d, object);
    }
  }

  // gather the data on what geometry will be present in the scene
  std::string geometrySubtype = getParamString("geometrySubtype", "triangle");
  std::string primitiveMode = getParamString("primitiveMode", "soup");
  int primitiveCount = getParam<int>("primitiveCount", 20);
  std::string shape = getParamString("shape", "triangle");
  int seed = getParam<int>("seed", 0);
  std::optional<int32_t> vertexCaps = std::nullopt;
  if (hasParam("vertexCaps")) {
    vertexCaps = getParam<int32_t>("vertexCaps", 0);
  }
  std::string globalCaps = getParamString("globalCaps", "none");
  std::optional<float> globalRadius = std::nullopt;
  if (hasParam("globalRadius")) {
    globalRadius = getParam<float>("globalRadius", 1.0f);
  }
  bool unusedVertices = getParam<bool>("unusedVertices", false);
  std::string colorAttribute = getParamString("color", "");
  std::string opacityAttribute = getParamString("opacity", "");

  // build this scene top-down to stress commit ordering guarantees
  // setup lighting, material and empty geometry

  auto surface = anari::newObject<anari::Surface>(d);
  // create geometry
  ANARIObject geom;
  if (auto it = m_anariObjects.find(ANARI_GEOMETRY);
      it != m_anariObjects.end() && !it->second.empty()) {
    geom = it->second.front();
  } else {
    createAnariObject(ANARI_GEOMETRY, geometrySubtype);
    geom = m_currentObject;
  }

  if (auto it = m_anariObjects.find(int(ANARI_MATERIAL));
      it != m_anariObjects.end()) {
    if (!it->second.empty()) {
      auto mat = it->second.front();
      anari::setParameter(d, surface, "material", mat);
    }
  }

  anari::setAndReleaseParameter(
      d, m_world, "surface", anari::newArray1D(d, &surface));

  anari::commitParameters(d, m_world);

  anari::setParameter(d, surface, "geometry", geom);

  // initialize PrimitiveGenerator with seed for random number generation
  PrimitiveGenerator generator(seed);


  // create all geometry depending on subtypes and shapes, indexed or soup
  // parameters vertex.position, vertex.radius, primitive.radius and primitive.index are set
  size_t componentCount = 3;
  if (geometrySubtype == "quad") {
    componentCount = 4;
  } else if (geometrySubtype == "sphere" || geometrySubtype == "curve") {
    componentCount = 1;
  } else if (geometrySubtype == "cone" || geometrySubtype == "cylinder") {
    componentCount = 2;
  }

  size_t indiciCount = 0;
  std::vector<glm::vec3> vertices;
  if (geometrySubtype == "triangle") { // handle all triangle geometry
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
      generator.shuffleVector(indices);
      if (unusedVertices && !indices.empty()) {
        // remove last indices to test not using all vertices/primitives
        indices.resize(indices.size() - 1);
      }
      indiciCount = indices.size();
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  } else if (geometrySubtype == "quad") { // handle all quad geometry
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
      generator.shuffleVector(indices);
      if (unusedVertices && !indices.empty()) {
        // remove last indices to test not using all vertices/primitives
        indices.resize(indices.size() - 1);
      }
      indiciCount = indices.size();
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  } else if (geometrySubtype == "sphere") {
    auto [sphereVertices, sphereRadii] =
        generator.generateSpheres(primitiveCount);
    vertices = sphereVertices;

    if (globalRadius.has_value()) {
      anari::setParameter(
          d, geom, "radius", globalRadius.value());
    } else {
      anari::setAndReleaseParameter(d,
          geom,
          "vertex.radius",
          anari::newArray1D(d, sphereRadii.data(), sphereRadii.size()));
    }

    if (primitiveMode == "indexed") {
      std::vector<uint32_t> indices;
      for (size_t i = 0; i < vertices.size(); ++i) {
        indices.push_back(static_cast<uint32_t>(i));
      }

      // shuffle indices vector to create a more useful test case
      generator.shuffleVector(indices);
      if (unusedVertices && !indices.empty()) {
        // remove last indices to test not using all vertices/primitives
        indices.resize(indices.size() - 1);
      }
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  } else if (geometrySubtype == "curve") {
    auto [curveVertices, curveRadii] =
        generator.generateCurves(primitiveCount);
    vertices = curveVertices;

    if (globalRadius.has_value()) {
      anari::setParameter(
          d, geom, "radius", globalRadius.value());
    } else {
      anari::setAndReleaseParameter(d,
          geom,
          "vertex.radius",
          anari::newArray1D(d, curveRadii.data(), curveRadii.size()));
    }

    if (primitiveMode == "indexed") {
      std::vector<uint32_t> indices;
      for (uint32_t i = 0; i < vertices.size() / 2; i++)
        indices.push_back(i * 2);

      // shuffle indices vector to create a more useful test case
      generator.shuffleVector(indices);
      if (unusedVertices && indices.size() >= 2) {
        // remove last indices to test not using all vertices/primitives
        indices.resize(indices.size() - 2);
      }
      indiciCount = indices.size();
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  } else if (geometrySubtype == "cone") {
    auto [coneVertices, coneRadii, coneCaps] =
        generator.generateCones(primitiveCount, vertexCaps);
    vertices = coneVertices;

    anari::setAndReleaseParameter(d,
        geom,
        "vertex.radius",
        anari::newArray1D(d, coneRadii.data(), coneRadii.size()));

    if (!coneCaps.empty()) {
      anari::setAndReleaseParameter(d,
          geom,
          "vertex.cap",
          anari::newArray1D(d, coneCaps.data(), coneCaps.size()));
    }

    anari::setParameter(d, geom, "caps", globalCaps);

    if (primitiveMode == "indexed") {
      std::vector<glm::uvec2> indices;
      for (uint32_t i = 0; i < vertices.size(); i += 2)
        indices.emplace_back(i, i + 1);

      // shuffle indices vector to create a more useful test case
      generator.shuffleVector(indices);
      if (unusedVertices && !indices.empty()) {
        // remove last indices to test not using all vertices/primitives
        indices.resize(indices.size() - 1);
      }
      indiciCount = indices.size();
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  } else if (geometrySubtype == "cylinder") {
    auto [cylinderVertices, cylinderRadii, cylinderCaps] =
        generator.generateCylinders(primitiveCount, vertexCaps);
    vertices = cylinderVertices;

    if (globalRadius.has_value()) {
      anari::setParameter(d, geom, "radius", globalRadius.value());
    } else {
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.radius",
          anari::newArray1D(d, cylinderRadii.data(), cylinderRadii.size()));
    }

    if (!cylinderCaps.empty()) {
      anari::setAndReleaseParameter(d,
          geom,
          "vertex.cap",
          anari::newArray1D(d, cylinderCaps.data(), cylinderCaps.size()));
    }

    anari::setParameter(d, geom, "caps", globalCaps);

    if (primitiveMode == "indexed") {
      std::vector<glm::uvec2> indices;
      for (uint32_t i = 0; i < vertices.size(); i += 2)
        indices.emplace_back(i, i + 1);

      // shuffle indices vector to create a more useful test case
      generator.shuffleVector(indices);
      if (unusedVertices && !indices.empty()) {
        // remove last indices to test not using all vertices/primitives
        indices.resize(indices.size() - 1);
      }
      indiciCount = indices.size();
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  }

  if (!colorAttribute.empty()) {
    size_t colorCount = vertices.size();
    if (colorAttribute.rfind("primitive", 0) != std::string::npos) {
        colorCount = primitiveCount;
    }

    std::vector<glm::vec3> attributeColor =
        colors::getColorVectorFromPalette(colorCount);

    anari::setAndReleaseParameter(d,
        geom,
        colorAttribute.c_str(),
        anari::newArray1D(d, attributeColor.data(), attributeColor.size()));
  }

  if (!opacityAttribute.empty()) {
    size_t opacityCount = vertices.size();
    if (colorAttribute.rfind("primitive", 0) != std::string::npos) {
      opacityCount = primitiveCount;
    }

    std::vector<float> attributeOpacity =
        generator.generateAttributeFloat(opacityCount, 0.0, 1.0);

    anari::setAndReleaseParameter(d,
        geom,
        opacityAttribute.c_str(),
        anari::newArray1D(d, attributeOpacity.data(), attributeOpacity.size()));
  }

  anari::setAndReleaseParameter(d,
      geom,
      "vertex.position",
      anari::newArray1D(d, vertices.data(), vertices.size()));

  // generate vertex attributes and primitive attributes
  if (indiciCount == 0) {
    indiciCount = vertices.size() / componentCount;
  }
  float attributeMin = getParam<float>("attribute_min", 0.0f);
  float attributeMax = getParam<float>("attribute_max", 1.0f);
  bool generateVertexAttributes = getParam<bool>("vertex_attributes", false);
  bool generatePrimitiveAttributes = getParam<bool>("primitive_attributes", false);
  if (generateVertexAttributes) {
    auto attributeFloat = generator.generateAttributeFloat(
        vertices.size(), attributeMin, attributeMax);
    anari::setAndReleaseParameter(d,
        geom,
        "vertex.attribute0",
        anari::newArray1D(d, attributeFloat.data(), attributeFloat.size()));

    auto attributeVec2 = generator.generateAttributeVec2(
        vertices.size(), attributeMin, attributeMax);
    anari::setAndReleaseParameter(d,
        geom,
        "vertex.attribute1",
        anari::newArray1D(d, attributeVec2.data(), attributeVec2.size()));

    auto attributeVec3 = generator.generateAttributeVec3(
        vertices.size(), attributeMin, attributeMax);
    anari::setAndReleaseParameter(d,
        geom,
        "vertex.attribute2",
        anari::newArray1D(d, attributeVec3.data(), attributeVec3.size()));

    auto attributeVec4 = generator.generateAttributeVec4(
        vertices.size(), attributeMin, attributeMax);
    anari::setAndReleaseParameter(d,
        geom,
        "vertex.attribute3",
        anari::newArray1D(d, attributeVec4.data(), attributeVec4.size()));
  }
  if (generatePrimitiveAttributes) {
    auto attributeFloat = generator.generateAttributeFloat(
        indiciCount, attributeMin, attributeMax);
    anari::setAndReleaseParameter(d,
        geom,
        "primitive.attribute0",
        anari::newArray1D(d, attributeFloat.data(), attributeFloat.size()));

    auto attributeVec2 = generator.generateAttributeVec2(
        indiciCount, attributeMin, attributeMax);
    anari::setAndReleaseParameter(d,
        geom,
        "primitive.attribute1",
        anari::newArray1D(d, attributeVec2.data(), attributeVec2.size()));

    auto attributeVec3 = generator.generateAttributeVec3(
        indiciCount, attributeMin, attributeMax);
    anari::setAndReleaseParameter(d,
        geom,
        "primitive.attribute2",
        anari::newArray1D(d, attributeVec3.data(), attributeVec3.size()));

    auto attributeVec4 = generator.generateAttributeVec4(
          indiciCount, attributeMin, attributeMax);
    anari::setAndReleaseParameter(d,
        geom,
        "primitive.attribute3",
        anari::newArray1D(d, attributeVec4.data(), attributeVec4.size()));
  }

  // commit everything to the device
  anari::commitParameters(d, geom);
  anari::commitParameters(d, surface);

  // cleanup
  anari::release(d, surface);
  anari::release(d, geom);
}

// render the scene with the given rendererType and renderDistance
// commit() needs to be called before this
// returns color and/or depth image data
std::vector<std::vector<uint32_t>> SceneGenerator::renderScene(float renderDistance)
{
  // gather previously set parameters for rendering this scene
  size_t image_height = getParam<int32_t>("image_height", 1024);
  size_t image_width = getParam<int32_t>("image_width", 1024);

  std::string color_type_param = getParamString("frame_color_type", "");
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

  std::string depth_type_param = getParamString("frame_depth_type", "");

  // create render camera
  ANARIObject camera;
  if (auto it = m_anariObjects.find(ANARI_CAMERA);
      it != m_anariObjects.end() && !it->second.empty()) {
    camera = it->second.front();
  } else {
    createAnariObject(ANARI_CAMERA, "perspective");
    camera = m_currentObject;
  }

  // create renderer
  ANARIObject renderer;
  if (auto it = m_anariObjects.find(ANARI_RENDERER);
      it != m_anariObjects.end() && !it->second.empty()) {
    renderer = it->second.front();
  } else {
    createAnariObject(ANARI_RENDERER, "default");
    renderer = m_currentObject;
    std::array<float, 4> bgColor = {0.f, 0.f, 0.f, 1.f};
    std::array<float, 3> ambientColor = {1.f, 1.f, 1.f};
    anari::setParameter(m_device, renderer, "background", bgColor); // white
    anari::setParameter(m_device, renderer, "ambientRadiance", 0.0f);
    anari::setParameter(m_device, renderer, "ambientColor", ambientColor);
    anari::commitParameters(m_device, renderer);
  }
 // anari::setParameter(m_device, renderer, "pixelSamples", 10);

  // setup frame
  auto frame = anari::newObject<anari::Frame>(m_device);
  anari::setParameter(m_device, frame, "size", glm::uvec2(image_height, image_width));
  if (color_type != ANARI_UNKNOWN) {
    anari::setParameter(m_device, frame, "channel.color", color_type);
  }
  if (depth_type_param == "FLOAT32") {
    anari::setParameter(m_device, frame, "channel.depth", ANARI_FLOAT32);
  }


  anari::setParameter(m_device, frame, "renderer", renderer);
  anari::setParameter(m_device, frame, "camera", camera);
  anari::setParameter(m_device, frame, "world", m_world);

  anari::commitParameters(m_device, frame);

  if (getParam<bool>("camera_generate_transform", true)) {
    // setup camera transform
    auto cam = createDefaultCameraFromWorld(m_world);
    anari::setParameter(m_device, camera, "position", cam.position);
    anari::setParameter(m_device, camera, "direction", cam.direction);
    anari::setParameter(m_device, camera, "up", cam.up);
    anari::commitParameters(m_device, camera);
  }

  std::vector<std::vector<uint32_t>> result;

  // render scene
  if (getParam<bool>("frameCompletionCallback", false)) {
    bool wasCalled = false;
    auto func = [](const void* userData, ANARIDevice, ANARIFrame)
    {       
        *(static_cast<bool *>(const_cast<void *>(userData))) = true;
    };
    anari::setParameter(m_device, frame, "frameCompletionCallback", static_cast<ANARIFrameCompletionCallback>(func));
    anari::setParameter(m_device, frame, "frameCompletionCallbackUserData", static_cast<void*>(&wasCalled));
    anari::commitParameters(m_device, frame);
    anari::render(m_device, frame);
    anari::wait(m_device, frame);
    if (!wasCalled) {
      uint32_t rgba = (255 << 24) + 255;
      std::vector<uint32_t> errorImage(image_height * image_width, rgba);
      result.emplace_back(errorImage);
      result.emplace_back(errorImage);
      // set frame duration member of this with last renderering's frame time
      if (!anariGetProperty(m_device,
              frame,
              "duration",
              ANARI_FLOAT32,
              &frameDuration,
              sizeof(frameDuration),
              ANARI_WAIT)) {
        frameDuration = -1.0f;
      }
      return result;
    }
  } else {
    anari::render(m_device, frame);
    anari::wait(m_device, frame);
  }

  // handle color output
  if (color_type != ANARI_UNKNOWN) {
    if (color_type == ANARI_FLOAT32_VEC4) {
      // handling of float data type
      const float *pixels = anari::map<float>(m_device, frame, "channel.color").data;
      std::vector<uint32_t> converted;
      if (pixels != nullptr) {
        for (int i = 0; i < image_height * image_width; ++i) {
          uint32_t rgba = 0;
          for (int j = 0; j < componentBytes; ++j) {
            uint8_t colorValue =
                static_cast<uint8_t>(pixels[i * componentBytes + j] * 255.0f);
            rgba += colorValue << (8 * j);
          }
          converted.push_back(rgba);
        }
      } else {
        printf("%s not supported\n", color_type_param.c_str());
      }

      result.emplace_back(converted);
      anari::unmap(m_device, frame, "channel.color");
    } else {
      // handling of other types
      auto fb = anari::map<uint32_t>(m_device, frame, "channel.color");
      if (fb.data != nullptr) {
        result.emplace_back(fb.data, fb.data + image_height * image_width);
      } else {
        printf("%s not supported\n", color_type_param.c_str());
      }
      anari::unmap(m_device, frame, "channel.color");
    }
  } else {
    // no color rendered
    result.emplace_back();
  }

  // handle depth output
  if (depth_type_param == "FLOAT32") {
    const float *pixels = anari::map<float>(m_device, frame, "channel.depth").data;

    std::vector<uint32_t> converted;
    if (pixels != nullptr) {
      for (int i = 0; i < image_height * image_width; ++i) {
        uint8_t colorValue =
            static_cast<uint8_t>(pixels[i] / renderDistance * 255.0f);
        uint32_t rgba =
            (255 << 24) + (colorValue << 16) + (colorValue << 8) + colorValue;
        converted.push_back(rgba);
      }
    } else {
      printf("Depth channel not supported\n");
    }

    result.emplace_back(converted);

    anari::unmap(m_device, frame, "channel.depth");
  } else {
    // no depth rendered
    result.emplace_back();
  }

  // set frame duration member of this with last renderering's frame time
  if (!anariGetProperty(m_device,
      frame,
      "duration",
      ANARI_FLOAT32,
          &frameDuration,
          sizeof(frameDuration),
          ANARI_WAIT)) {
    frameDuration = -1.0f;
  }

  // cleanup
  anari::release(m_device, frame);

  return result;
}

// clear any existing objects in the scene
void SceneGenerator::resetSceneObjects() {
  anari::unsetParameter(m_device, m_world, "instance");
  anari::unsetParameter(m_device, m_world, "surface");
  anari::unsetParameter(m_device, m_world, "volume");
  anari::unsetParameter(m_device, m_world, "light");
}

// reset all parameters and objects in the scene
void SceneGenerator::resetAllParameters() {
  resetSceneObjects();
  for (auto &[key, value] : m_anariObjects) {
    for (auto &object : value) {
      anari::release(m_device, object);
    }
  }
  m_anariObjects.clear();
  std::vector<std::string> paramNames;
  for (auto it = params_begin(); it != params_end(); ++it) {
    paramNames.push_back(it->first);
  }
  for (auto &name : paramNames) {
    removeParam(name);
  }
}

// returns vector of bounds containing (in order)
// world bounds, instances bounds, group bounds
// each containing a set of min and max bounds
// one set for the world, one per instance and one per group
std::vector<std::vector<std::vector<std::vector<float>>>> SceneGenerator::getBounds()
{
  std::vector<std::vector<std::vector<std::vector<float>>>> result;
  std::vector<std::vector<std::vector<float>>> worldBounds;
  std::vector<std::vector<std::vector<float>>> instancesBounds;
  std::vector<std::vector<std::vector<float>>> groupBounds;
  std::vector<std::vector<float>> &singleBound =
      worldBounds.emplace_back();
  // gather world bounds
  auto anariWorldBounds = bounds();
  for (const auto &bound : anariWorldBounds) {
    std::vector<float>& vector = singleBound.emplace_back();
    for (int i = 0; i < 3; ++i) {
      vector.push_back(bound[i]);
    }
  }

  // gather bounds per instance
  if (m_anariObjects.find(int(ANARI_INSTANCE)) != m_anariObjects.end()) {
    for (auto &instance : m_anariObjects[int(ANARI_INSTANCE)]) {
      singleBound = instancesBounds.emplace_back();
      anari::scenes::box3 anariInstanceBounds;
      anari::getProperty(m_device, instance, "bounds", anariInstanceBounds);
      for (const auto &bound : anariInstanceBounds) {
        std::vector<float> &vector = singleBound.emplace_back();
        for (int i = 0; i < anari::length(bound); ++i) {
          vector.push_back(bound[i]);
        }
      }
    }
  }

  // gather bounds per group
  if (m_anariObjects.find(int(ANARI_GROUP)) != m_anariObjects.end()) {
    for (auto &group : m_anariObjects[int(ANARI_GROUP)]) {
      singleBound = groupBounds.emplace_back();
      anari::scenes::box3 anariGroupBounds;
      anari::getProperty(m_device, group, "bounds", anariGroupBounds);
      for (const auto &bound : anariGroupBounds) {
        std::vector<float> &vector = singleBound.emplace_back();
        for (int i = 0; i < anari::length(bound); ++i) {
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
