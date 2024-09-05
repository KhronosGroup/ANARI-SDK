// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SceneGenerator.h"
#include "PrimitiveGenerator.h"
#include "ColorPalette.h"
#include "TextureGenerator.h"
#include "anariWrapper.h"
#include "anari/frontend/type_utility.h"

#include <stdexcept>

namespace cts {

SceneGenerator::SceneGenerator(
    anari::Device device)
    : TestScene(device), m_gltf(device)
{
  m_frame = anari::newObject<anari::Frame>(m_device);
  m_world = anari::newObject<anari::World>(m_device);
}

SceneGenerator::~SceneGenerator()
{
  for (auto& [key, value] : m_anariObjects) {
    for (auto &object : value) {
      anari::release(m_device, object);
    }
  }
  anari::release(m_device, m_frame);
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
      {"frame_albedo_type", "", "This setting will render the albedo channel instead of the color channel if KHR_FRAME_CHANNEL_ALBEDO is supported. Possible values: UFIXED8_VEC3, UFIXED8_RGB_SRGB, FLOAT32_VEC3"},
      {"frame_normal_type", "", "This setting will render the normal channel instead of the color channel if KHR_FRAME_CHANNEL_NORMAL is supported. Possible values: FIXED16_VEC3, FLOAT32_VEC3"},
      {"frame_primitiveId_type", "", "This setting will render the primitive ID channel as colors instead of the color channel if KHR_FRAME_CHANNEL_PRIMITIVE_ID is supported. Possible values: UINT32"},
      {"frame_objectId_type", "", "This setting will render the object ID channel as colors instead of the color channel if KHR_FRAME_CHANNEL_OBJECT_ID is supported. Possible values: UINT32"},
      {"frame_instanceId_type", "", "This setting will render the instance ID channel as colors instead of the color channel if KHR_FRAME_CHANNEL_INSTANCE_ID is supported. Possible values: UINT32"},
      {"image_height", 1024, "Height of the image"},
      {"image_width", 1024, "Width of the image"},
      {"attribute_min", 0.0f, "Minimum random value for attributes"},
      {"attribute_max", 1.0f, "Maximum random value for attributes"},
      {"primitive_attributes", false, "If primitive attributes should be filled randomly"},
      {"vertex_attributes", false, "If vertex attributes should be filled randomly"},
      {"seed", 0u, "Seed for random number generator to ensure that tests are consistent across platforms"},
      {"vertexCaps", false, "Should cones and cylinders have caps (per vertex setting)"},
      {"globalCaps", "none", "Should cones and cylinders have caps (global setting). Possible values: \"none\", \"first\", \"second\", \"both\""},
      {"globalRadius", 1.0f, "Use the global radius property instead of a per vertex one"},
      {"unusedVertices", false, "The last primitive's indices in the index buffer will be removed to test handling of unused/skipped vertices in the vertex buffer"},
      {"color", "", "Fill an attribute with colors. Possible values: \"vertex.color\", \"vertex.attribute0\", \"primitive.attribute3\" and similar"},
      {"opacity", "", "Fill an attribute with opacity values. Possible values: \"vertex.attribute0\", \"primitive.attribute3\" and similar"},
      {"spatial_field_dimensions", std::array<uint32_t, 3>{0, 0, 0}, "Dimensions of the spatial field"},
      {"frameCompletionCallback", false, "Enables test for ANARI_KHR_FRAME_COMPLETION_CALLBACK. A red image is rendered on error."},
      {"progressiveRendering", false, "Enables test for ANARI_KHR_PROGRESSIVE_RENDERING. A green image is rendered if the render improved a red image otherwise."},
      {"gltf_camera", -1, "glTF camera to use to render the scene"},
      {"gltf_file", "", "path to glTF"}

      //
  };
}

void SceneGenerator::loadGLTF(const std::string &jsonText,
    std::vector<std::vector<char>> &sortedBuffers,
    std::vector<std::vector<char>> &sortedImages)
{
  m_gltf = gltf_data(m_device);
  m_gltf.parse_glTF(jsonText, sortedBuffers, sortedImages);
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
  if (type == "volume") {
    return ANARI_VOLUME;
  }
  if (type == "spatialField") {
    return ANARI_SPATIAL_FIELD;
  }
  if (type == "instance") {
    return ANARI_INSTANCE;
  }
  if (type == "group") {
    return ANARI_GROUP;
  }
  if (type == "surface") {
    return ANARI_SURFACE;
  }
  if (type == "UFIXED8_VEC4") {
    return ANARI_UFIXED8_VEC4;
  }
  if (type == "UFIXED8_RGBA_SRGB") {
    return ANARI_UFIXED8_RGBA_SRGB;
  }
  if (type == "FLOAT32_VEC4") {
    return ANARI_FLOAT32_VEC4;
  }
  if (type == "FLOAT32") {
    return ANARI_FLOAT32;
  }
  if (type == "FIXED16_VEC3") {
    return ANARI_FIXED16_VEC3;
  }
  if (type == "FLOAT32_VEC3") {
    return ANARI_FLOAT32_VEC3;
  }
  if (type == "UFIXED8_VEC3") {
    return ANARI_UFIXED8_VEC3;
  }
  if (type == "UINT32") {
    return ANARI_UINT32;
  }
  return ANARI_UNKNOWN;
}

void SceneGenerator::setGenericTexture2D(
    const std::string &name, const std::string &textureType)
{
    size_t resolution = 32;
    auto pixels = TextureGenerator::generateCheckerBoard(resolution);
    if (m_device && m_currentObject) {
      anari::setAndReleaseParameter(m_device,
          m_currentObject,
          name.c_str(),
          anari::newArray2D(m_device, pixels.data(), resolution, resolution));
    }
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

void SceneGenerator::setReferenceArray(int objectType,
    size_t objectIndex,
    const std::string &name,
    int refType,
    const std::vector<size_t>& refIndices)
{
  if (auto itObj = m_anariObjects.find(objectType);
      itObj != m_anariObjects.end() && objectIndex < itObj->second.size()) {
    auto object = itObj->second[objectIndex];
    if (m_device != nullptr) {
      if (auto itRef = m_anariObjects.find(refType);
          itRef != m_anariObjects.end()) {
        std::vector<ANARIObject> references;
        for (size_t ref : refIndices) {
          if (ref >= itRef->second.size()) {
            throw std::runtime_error("Reference index out of range");
          }
          references.push_back(itRef->second[ref]);
        }
        anari::setAndReleaseParameter(m_device,
            object,
            name.c_str(),
            anari::newArray1D(m_device, references.data(), references.size()));
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
    int type, const std::string &subtype, const std::string& ctsType)
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
    if (subtype == "hdri") {
      size_t resolution = 64;
      auto checkerboard = TextureGenerator::generateCheckerBoardHDR(resolution);
      anari::setAndReleaseParameter(m_device,
          object,
          "radiance",
          anari::newArray2D(
              m_device, checkerboard.data(), resolution, resolution));
    }
    break;
  }
  case ANARI_VOLUME: {
    object = anari::newObject<anari::Volume>(m_device, subtype.c_str());
    auto it = m_anariObjects.try_emplace(
        int(ANARI_VOLUME), std::vector<ANARIObject>());
    it.first->second.emplace_back(object);
    break;
  }
  case ANARI_SPATIAL_FIELD: {
    object = anari::newObject<anari::SpatialField>(m_device, subtype.c_str());
    auto it = m_anariObjects.try_emplace(
        int(ANARI_SPATIAL_FIELD), std::vector<ANARIObject>());
        it.first->second.emplace_back(object);
    break;
  }
  case ANARI_INSTANCE: {
    object = anari::newObject<anari::Instance>(m_device, subtype.c_str());
    auto it = m_anariObjects.try_emplace(
        int(ANARI_INSTANCE), std::vector<ANARIObject>());
    it.first->second.emplace_back(object);
    break;
  }
  case ANARI_GROUP: {
    object = anari::newObject<anari::Group>(m_device);
    auto it = m_anariObjects.try_emplace(
        int(ANARI_GROUP), std::vector<ANARIObject>());
    it.first->second.emplace_back(object);
    break;
  }
  case ANARI_SURFACE: {
    object = anari::newObject<anari::Surface>(m_device);
    auto it = m_anariObjects.try_emplace(
        int(ANARI_SURFACE), std::vector<ANARIObject>());
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
      std::vector<anari::math::float4> greyscale =
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
  std::string geometrySubtype = getParamString("geometrySubtype", "");
  std::string primitiveMode = getParamString("primitiveMode", "soup");
  std::array<uint32_t, 3> spatialFieldDim =
      getParam<std::array<uint32_t, 3>>("spatial_field_dimensions", {0, 0, 0});
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

  // initialize PrimitiveGenerator with seed for random number generation
  PrimitiveGenerator generator(seed);

  // build this scene top-down to stress commit ordering guarantees
  // setup lighting, material and empty geometry

  std::vector<ANARIObject> instances;
  if (auto it = m_anariObjects.find(ANARI_INSTANCE);
      it != m_anariObjects.end() && !it->second.empty()) {
    instances = it->second;
  }

  // create geometry
  std::vector<ANARIObject> geoms;
  std::vector<ANARIObject> surfaces;
  if (auto it = m_anariObjects.find(ANARI_GEOMETRY);
      it != m_anariObjects.end() && !it->second.empty()) {
    geoms = it->second;
  } else if (geometrySubtype != "") {
    createAnariObject(ANARI_GEOMETRY, geometrySubtype);
    geoms.push_back(m_currentObject);
  }

  bool createSurfaces = false;
  auto surfaceIt = m_anariObjects.find(ANARI_SURFACE);
  if (surfaceIt != m_anariObjects.end() && !surfaceIt->second.empty()) {
    surfaces = surfaceIt->second;
  } else {
    createSurfaces = true;
  }

  for (size_t i = 0; i < geoms.size(); ++i) {
    auto geom = geoms[i];
    if (createSurfaces){
      createAnariObject(ANARI_SURFACE, "");
      ANARIObject surface = m_currentObject;
      anari::setParameter(d, surface, "geometry", geom);
      surfaces.push_back(surface);
      if (auto it = m_anariObjects.find(int(ANARI_MATERIAL));
          it != m_anariObjects.end()) {
        if (i < it->second.size()) {
          auto mat = it->second[i];
          anari::setParameter(d, surface, "material", mat);
        }
      }
      anari::commitParameters(d, surface);
    }

    // create all geometry depending on subtypes and shapes, indexed or soup
    // parameters vertex.position, vertex.radius, primitive.radius and
    // primitive.index are set
    size_t componentCount = 3;
    if (geometrySubtype == "quad") {
      componentCount = 4;
    } else if (geometrySubtype == "sphere" || geometrySubtype == "curve") {
      componentCount = 1;
    } else if (geometrySubtype == "cone" || geometrySubtype == "cylinder") {
      componentCount = 2;
    }

    size_t indiciCount = 0;
    std::vector<anari::math::float3> vertices;
    if (geometrySubtype == "triangle") { // handle all triangle geometry
      std::vector<anari::math::vec<uint32_t, 3>> indices;
      if (shape == "triangle") {
        vertices = generator.generateTriangles(primitiveCount);

        if (primitiveMode == "indexed") {
          for (size_t i = 0; i < vertices.size(); i += 3) {
            const unsigned int index = static_cast<unsigned int>(i);
            indices.push_back(
                anari::math::vec<uint32_t, 3>(index, index + 1u, index + 2u));
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
      std::vector<anari::math::vec<uint32_t, 4>> indices;
      if (shape == "quad") {
        vertices = generator.generateQuads(primitiveCount);

        if (primitiveMode == "indexed") {
          for (size_t i = 0; i < vertices.size(); i += 4) {
            const unsigned int index = static_cast<unsigned int>(i);
            indices.push_back(anari::math::vec<uint32_t, 4>(
                index, index + 1u, index + 2u, index + 3u));
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
        anari::setParameter(d, geom, "radius", globalRadius.value());
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
        anari::setParameter(d, geom, "radius", globalRadius.value());
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
        std::vector<anari::math::vec<uint32_t, 2>> indices;
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
        std::vector<anari::math::vec<uint32_t, 2>> indices;
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

      std::vector<anari::math::float3> attributeColor =
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
          anari::newArray1D(
              d, attributeOpacity.data(), attributeOpacity.size()));
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
    bool generatePrimitiveAttributes =
        getParam<bool>("primitive_attributes", false);
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
  }

  if (!surfaces.empty() && instances.empty()) {
    anari::setAndReleaseParameter(
        d, m_world, "surface", anari::newArray1D(d, surfaces.data(), surfaces.size()));
  }

  if (auto lightIt = m_anariObjects.find(ANARI_LIGHT);
      instances.empty() && lightIt != m_anariObjects.end() && !lightIt->second.empty()) {
    anari::setAndReleaseParameter(d,
        m_world,
        "light",
        anari::newArray1D(d, lightIt->second.data(), lightIt->second.size()));
  }

  if (spatialFieldDim[0] != 0 && spatialFieldDim[1] != 0
      && spatialFieldDim[2] != 0) {
    // create spatial field
    std::vector<ANARIObject> volumes;
    std::vector<ANARIObject> spatialFields;
    bool createSpatialFields = false;

    if (auto it = m_anariObjects.find(ANARI_VOLUME);
        it != m_anariObjects.end() && !it->second.empty()) {
      volumes = it->second;
    }

    auto fieldIt = m_anariObjects.find(ANARI_SPATIAL_FIELD);
    if (fieldIt == m_anariObjects.end() || fieldIt->second.empty()) {
      createSpatialFields = true;
    } else {
      spatialFields = fieldIt->second;
    }

    for (auto volume : volumes) {
      if (createSpatialFields) {
        createAnariObject(ANARI_SPATIAL_FIELD, "structuredRegular");
        auto spatialField = m_currentObject;
        anari::setParameter(m_device, volume, "value", spatialField);
        spatialFields.push_back(spatialField);
        anari::commitParameters(d, volume);
      }
    }

    if (!volumes.empty() && instances.empty()) {
      anari::setAndReleaseParameter(d,
          m_world,
          "volume",
          anari::newArray1D(d, volumes.data(), volumes.size()));
    }

    for (auto spatialField : spatialFields) {
      std::vector<float> data = generator.generateAttributeFloat(
          static_cast<size_t>(spatialFieldDim[0]) * spatialFieldDim[1] * spatialFieldDim[2]);
      anari::setAndReleaseParameter(m_device,
          spatialField,
          "data",
          anari::newArray3D(m_device,
              data.data(),
              spatialFieldDim[0],
              spatialFieldDim[1],
              spatialFieldDim[2]));
      anari::commitParameters(d, spatialField);
    }
  }

  std::string glTFPath = getParamString("gltf_file", "");
  if (!glTFPath.empty()) {
    instances.insert(instances.end(),
        m_gltf.instances[0].begin(),
        m_gltf.instances[0].end());
  }

  if (!instances.empty()) {
    anari::setAndReleaseParameter(d,
        m_world,
        "instance",
        anari::newArray1D(d, instances.data(), instances.size()));
  }

  anari::commitParameters(d, m_world);
}

// render the scene with the given rendererType and renderDistance
// commit() needs to be called before this
// returns color and/or depth image data
std::vector<std::vector<uint32_t>> SceneGenerator::renderScene(float renderDistance)
{
  // gather previously set parameters for rendering this scene
  uint32_t image_height = getParam<uint32_t>("image_height", 1024);
  uint32_t image_width = getParam<uint32_t>("image_width", 1024);
  std::string channelName = "";
  ANARIDataType color_type;
  bool normalChannel = false;
  std::string channel_type_param = getParamString("frame_instanceId_type", "");
  if (!channel_type_param.empty()) {
    channelName = "channel.instanceId";
  } else if (channel_type_param = getParamString("frame_normal_type", "");
             !channel_type_param.empty()) {
    channelName = "channel.normal";
    normalChannel = true;
  } else if (channel_type_param = getParamString("frame_albedo_type", "");
             !channel_type_param.empty()) {
    channelName = "channel.albedo";
  } else if (channel_type_param = getParamString("frame_primitiveId_type", "");
             !channel_type_param.empty()) {
    channelName = "channel.primitiveId";
  } else if (channel_type_param = getParamString("frame_objectId_type", "");
             !channel_type_param.empty()) {
    channelName = "channel.objectId";
  } else if (channel_type_param = getParamString("frame_color_type", "");
             !channel_type_param.empty()) {
    channelName = "channel.color";
  }
  color_type = anariTypeFromString(channel_type_param);

  std::string depth_type_param = getParamString("frame_depth_type", "");

  // create render camera
  const int gltf_camera_index = getParam<int>("gltf_camera", -1);
  if (!m_gltf.cameras.empty() && gltf_camera_index != -1) {
    anari::setParameter(
        m_device, m_frame, "camera", m_gltf.cameras[gltf_camera_index]);
  } else {
    ANARIObject camera;
    if (auto it = m_anariObjects.find(ANARI_CAMERA);
        it != m_anariObjects.end() && !it->second.empty()) {
      camera = it->second.front();
    } else {
      createAnariObject(ANARI_CAMERA, "perspective");
      camera = m_currentObject;
      
      auto cameraTransform = createDefaultCameraFromWorld();
      anari::setParameter(m_device, camera, "position", cameraTransform.position);
      anari::setParameter(m_device, camera, "direction", cameraTransform.direction);
      anari::setParameter(m_device, camera, "up", cameraTransform.up);
      anari::commitParameters(m_device, camera);
    }
    anari::setParameter(m_device, m_frame, "camera", camera);
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
  anari::setParameter(m_device, m_frame, "size", anari::math::vec<uint32_t, 2>(static_cast<uint32_t>(image_height), static_cast<uint32_t>(image_width)));
  if (color_type != ANARI_UNKNOWN) {
    anari::setParameter(m_device, m_frame, channelName.c_str(), color_type);
  }
  if (depth_type_param == "FLOAT32") {
    anari::setParameter(m_device, m_frame, "channel.depth", ANARI_FLOAT32);
  }


  anari::setParameter(m_device, m_frame, "renderer", renderer);
  anari::setParameter(m_device, m_frame, "world", m_world);

  anari::commitParameters(m_device, m_frame);

  std::vector<std::vector<uint32_t>> result;

  // render scene
  if (getParam<bool>("frameCompletionCallback", false)) {
    bool wasCalled = false;
    auto func = [](const void* userData, ANARIDevice, ANARIFrame)
    {
        *(static_cast<bool *>(const_cast<void *>(userData))) = true;
    };
    anari::setParameter(m_device, m_frame, "frameCompletionCallback", static_cast<ANARIFrameCompletionCallback>(func));
    anari::setParameter(m_device,
        m_frame,
        "frameCompletionCallbackUserData",
        static_cast<void *>(&wasCalled));
    anari::commitParameters(m_device, m_frame);
    anari::render(m_device, m_frame);
    anari::wait(m_device, m_frame);
    
    uint32_t rgba;
    if (wasCalled) {
      rgba = (255 << 24) + (255 << 8);
    } else {
      rgba = (255 << 24) + 255;
    }

    std::vector<uint32_t> errorImage(image_height * image_width, rgba);
    result.emplace_back(errorImage);
    result.emplace_back(errorImage);
    // set frame duration member of this with last renderering's frame time
    if (!anariGetProperty(m_device,
            m_frame,
            "duration",
            ANARI_FLOAT32,
            &frameDuration,
            sizeof(frameDuration),
            ANARI_WAIT)) {
      frameDuration = -1.0f;
    }
    return result;
  } else if (getParam<bool>("progressiveRendering", false)) {
    if (color_type == ANARI_FLOAT32_VEC4) {
      throw std::runtime_error("ANARI_FLOAT32_VEC4 not supported for frameProgressiveRendering test");
    }
    anari::render(m_device, m_frame);
    anari::wait(m_device, m_frame);
    std::vector<uint32_t> firstImage;
    std::vector<uint32_t> accumulatedImage;
    auto fb = anari::map<uint32_t>(m_device, m_frame, "channel.color");
    if (fb.data != nullptr) {
      firstImage.assign(fb.data, fb.data + image_height * image_width);
    } else {
      printf("%s not supported\n", channel_type_param.c_str());
    }
    anari::unmap(m_device, m_frame, "channel.color");

    for (int frames = 0; frames < 10; frames++) {
      anari::render(m_device, m_frame);
      anari::wait(m_device, m_frame);
    }
    fb = anari::map<uint32_t>(m_device, m_frame, "channel.color");
    if (fb.data != nullptr) {
      accumulatedImage.assign(fb.data, fb.data + image_height * image_width);
    } else {
      printf("%s not supported\n", channel_type_param.c_str());
    }
    anari::unmap(m_device, m_frame, "channel.color");

    if (firstImage.size() != accumulatedImage.size()) {
      throw std::runtime_error(
          "Images have different sizes");
    }

    size_t changedPixels = 0;
    for (size_t i = 0; i < firstImage.size(); ++i) {
      if (firstImage[i] != accumulatedImage[i]) {
        ++changedPixels;
      }
    }

    uint32_t rgba;
    if (changedPixels > 10) {
      rgba = (255 << 24) + (255 << 8);
    } else {
      rgba = (255 << 24) + 255;
    }
    std::vector<uint32_t> resultImage(image_height * image_width, rgba);
    result.emplace_back(resultImage);

   if (!anariGetProperty(m_device,
            m_frame,
            "duration",
            ANARI_FLOAT32,
            &frameDuration,
            sizeof(frameDuration),
            ANARI_WAIT)) {
      frameDuration = -1.0f;
    }

    return result;

  } else
  {
    anari::render(m_device, m_frame);
    anari::wait(m_device, m_frame);
  }

  size_t componentCount = anari::componentsOf(color_type);
  // handle color output
  if (color_type != ANARI_UNKNOWN) {
    if (color_type == ANARI_FLOAT32_VEC4 || color_type == ANARI_FLOAT32_VEC3) {
      // handling of float data type
      const float *pixels =
          anari::map<float>(m_device, m_frame, channelName.c_str()).data;
      std::vector<uint32_t> converted;
      if (pixels != nullptr) {
        for (uint32_t i = 0; i < image_height * image_width; ++i) {
          uint32_t rgba = 0;
          for (uint32_t j = 0; j < 4; ++j) {
            uint8_t colorValue = j == 4 ? 255 : 0;
            if (j <= componentCount) {
              if (normalChannel) {
                colorValue = static_cast<uint8_t>(
                    TextureGenerator::convertNormalToColor(pixels[i * componentCount + j], j == 3) * 255.0f);
              } else {
                colorValue = static_cast<uint8_t>(
                    pixels[i * componentCount + j] * 255.0f);
              }
            }
            rgba += colorValue << (8 * j);
          }
          converted.push_back(rgba);
        }
      } else {
        printf("%s not supported\n", channel_type_param.c_str());
      }

      result.emplace_back(converted);
      anari::unmap(m_device, m_frame, channelName.c_str());
    } else {
      if (componentCount == 4
          && anari::sizeOf(color_type) == sizeof(uint32_t)) {
        // handling of other types
        const uint32_t *pixels =
            anari::map<uint32_t>(m_device, m_frame, channelName.c_str()).data;
        if (pixels != nullptr) {
          result.emplace_back(pixels, pixels + image_height * image_width);
        } else {
          printf("%s not supported\n", channel_type_param.c_str());
        }
        anari::unmap(m_device, m_frame, channelName.c_str());
      } else if (color_type == ANARI_UINT32) {
        const uint32_t *pixels =
            anari::map<uint32_t>(m_device, m_frame, channelName.c_str()).data;
        std::vector<uint32_t> converted;
        if (pixels != nullptr) {
          for (uint32_t i = 0; i < image_height * image_width; ++i) {
            auto colorValue = colors::getColorFromPalette(pixels[i]);
            uint32_t rgba = (255 << 24)
                + (static_cast<uint8_t>(colorValue.z * 255.0f) << 16)
                + (static_cast<uint8_t>(colorValue.y * 255.0f) << 8)
                + static_cast<uint8_t>(colorValue.x * 255.0f);
            converted.push_back(rgba);
          }
        } else {
          printf("%s not supported\n", channel_type_param.c_str());
        }
        result.emplace_back(converted);
        anari::unmap(m_device, m_frame, channelName.c_str());
      } else if (anari::sizeOf(color_type) / componentCount == sizeof(char)) {
        // 8bit component
        const uint8_t *pixels =
            anari::map<uint8_t>(m_device, m_frame, channelName.c_str()).data;
        std::vector<uint32_t> converted;
        if (pixels != nullptr) {
          for (uint32_t i = 0; i < image_height * image_width; ++i) {
            uint32_t rgba = 0;
            for (uint32_t j = 0; j < 4; ++j) {
              uint8_t colorValue = j == 4 ? 255 : 0;
              if (j <= componentCount) {
                colorValue = pixels[i * componentCount + j];
              }
              rgba += colorValue << (8 * j);
            }
            converted.push_back(rgba);
          }
        } else {
          printf("%s not supported\n", channel_type_param.c_str());
        }

        result.emplace_back(converted);
        anari::unmap(m_device, m_frame, channelName.c_str());
      } else if (anari::sizeOf(color_type) / componentCount == sizeof(char) * 2) {
        // 16bit component
        const int16_t *pixels =
            anari::map<int16_t>(m_device, m_frame, channelName.c_str()).data;
        std::vector<uint32_t> converted;
        if (pixels != nullptr) {
          for (uint32_t i = 0; i < image_height * image_width; ++i) {
            uint32_t rgba = 0;
            for (uint32_t j = 0; j < 4; ++j) {
              uint8_t colorValue = j == 4 ? 255 : 0;
              if (j <= componentCount) {
                colorValue = TextureGenerator::convertShortNormalToColor(pixels[i * componentCount + j], j == 3);
              }
              rgba += colorValue << (8 * j);
            }
            converted.push_back(rgba);
          }
        } else {
          printf("%s not supported\n", channel_type_param.c_str());
        }

        result.emplace_back(converted);
        anari::unmap(m_device, m_frame, channelName.c_str());
      }
    }
  } else {
    // no color rendered
    result.emplace_back();
  }

  // handle depth output
  if (depth_type_param == "FLOAT32") {
    const float *pixels =
        anari::map<float>(m_device, m_frame, "channel.depth").data;

    std::vector<uint32_t> converted;
    if (pixels != nullptr) {
      for (uint32_t i = 0; i < image_height * image_width; ++i) {
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

    anari::unmap(m_device, m_frame, "channel.depth");
  } else {
    // no depth rendered
    result.emplace_back();
  }

  // set frame duration member of this with last renderering's frame time
  if (!anariGetProperty(m_device,
          m_frame,
      "duration",
      ANARI_FLOAT32,
          &frameDuration,
          sizeof(frameDuration),
          ANARI_WAIT)) {
    frameDuration = -1.0f;
  }

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
  m_gltf = gltf_data(m_device);
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
        for (int i = 0; i < 3; ++i) {
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
        for (int i = 0; i < 3; ++i) {
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
