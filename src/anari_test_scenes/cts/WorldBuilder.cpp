// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "WorldBuilder.h"
#include "generators/ColorPalette.h"
#include "generators/PrimitiveGenerator.h"
#include "generators/TextureGenerator.h"
// std
#include <cstring>
#include <string>

namespace anari {
namespace cts {

namespace scenes = anari::scenes;

// --- Generic axis-value parameter binding ------------------------------------

namespace detail {

void setBoundParameterImpl(anari::Device d,
    anari::Object obj,
    const char *param,
    const Any &value,
    const std::vector<anari::Sampler> &samplers)
{
  if (!value.valid())
    return; // none() -> leave unset so the device default applies

  const ANARIDataType t = value.type();

  if (t == ANARI_STRING) {
    const std::string s = value.getString();
    const std::string refPrefix = "ref_sampler_";
    if (s.rfind(refPrefix, 0) == 0) {
      size_t idx = 0;
      try {
        idx = static_cast<size_t>(std::stoul(s.substr(refPrefix.size())));
      } catch (const std::exception &) {
        return;
      }
      if (idx < samplers.size()) {
        anari::Sampler sm = samplers[idx];
        anariSetParameter(d, obj, param, ANARI_SAMPLER, &sm);
      }
      return;
    }
    // A plain string is an attribute name (e.g. "color", "attribute0").
    anariSetParameter(d, obj, param, ANARI_STRING, s.c_str());
    return;
  }

  if (anari::isObject(t))
    return; // object handles are not carried as axis values

  // Scalars, vectors, matrices, boxes: set the parameter with its exact stored
  // type and raw bytes.
  anariSetParameter(d, obj, param, t, value.data());
}

} // namespace detail

// --- Geometry ----------------------------------------------------------------

anari::Geometry buildGeometry(anari::Device d, const GeometryOptions &opts)
{
  scenes::PrimitiveGenerator generator(opts.seed);

  auto geom = anari::newObject<anari::Geometry>(d, opts.subtype.c_str());

  const std::string &subtype = opts.subtype;
  const std::string &shape = opts.shape;
  const std::string &primitiveMode = opts.primitiveMode;
  const int primitiveCount = opts.primitiveCount;
  const bool indexed = primitiveMode == "indexed";

  size_t componentCount = 3;
  if (subtype == "quad")
    componentCount = 4;
  else if (subtype == "sphere" || subtype == "curve")
    componentCount = 1;
  else if (subtype == "cone" || subtype == "cylinder")
    componentCount = 2;

  size_t indiciCount = 0;
  std::vector<math::float3> vertices;

  if (subtype == "triangle") {
    std::vector<math::vec<uint32_t, 3>> indices;
    if (shape == "triangle") {
      vertices = generator.generateTriangles(primitiveCount);
      if (indexed) {
        for (size_t i = 0; i < vertices.size(); i += 3) {
          const auto idx = static_cast<unsigned int>(i);
          indices.push_back(math::vec<uint32_t, 3>(idx, idx + 1u, idx + 2u));
        }
      }
    } else if (shape == "quad") {
      if (indexed) {
        auto [v, i] = generator.generateTriangulatedQuadsIndexed(primitiveCount);
        vertices = v;
        indices = i;
      } else {
        vertices = generator.generateTriangulatedQuadsSoup(primitiveCount);
      }
    } else if (shape == "cube") {
      if (indexed) {
        auto [v, i] = generator.generateTriangulatedCubesIndexed(primitiveCount);
        vertices = v;
        indices = i;
      } else {
        vertices = generator.generateTriangulatedCubesSoup(primitiveCount);
      }
    }
    if (indexed) {
      generator.shuffleVector(indices);
      if (opts.unusedVertices && !indices.empty())
        indices.resize(indices.size() - 1);
      indiciCount = indices.size();
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  } else if (subtype == "quad") {
    std::vector<math::vec<uint32_t, 4>> indices;
    if (shape == "quad") {
      vertices = generator.generateQuads(primitiveCount);
      if (indexed) {
        for (size_t i = 0; i < vertices.size(); i += 4) {
          const auto idx = static_cast<unsigned int>(i);
          indices.push_back(
              math::vec<uint32_t, 4>(idx, idx + 1u, idx + 2u, idx + 3u));
        }
      }
    } else if (shape == "cube") {
      if (indexed) {
        auto [v, i] = generator.generateQuadCubesIndexed(primitiveCount);
        vertices = v;
        indices = i;
      } else {
        vertices = generator.generateQuadCubesSoup(primitiveCount);
      }
    }
    if (indexed) {
      generator.shuffleVector(indices);
      if (opts.unusedVertices && !indices.empty())
        indices.resize(indices.size() - 1);
      indiciCount = indices.size();
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  } else if (subtype == "sphere") {
    auto [v, radii] = generator.generateSpheres(primitiveCount);
    vertices = v;
    if (opts.globalRadius.has_value()) {
      anari::setParameter(d, geom, "radius", opts.globalRadius.value());
    } else {
      anari::setAndReleaseParameter(
          d, geom, "vertex.radius", anari::newArray1D(d, radii.data(), radii.size()));
    }
    if (indexed) {
      std::vector<uint32_t> indices;
      for (size_t i = 0; i < vertices.size(); ++i)
        indices.push_back(static_cast<uint32_t>(i));
      generator.shuffleVector(indices);
      if (opts.unusedVertices && !indices.empty())
        indices.resize(indices.size() - 1);
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  } else if (subtype == "curve") {
    auto [v, radii] = generator.generateCurves(primitiveCount);
    vertices = v;
    if (opts.globalRadius.has_value()) {
      anari::setParameter(d, geom, "radius", opts.globalRadius.value());
    } else {
      anari::setAndReleaseParameter(
          d, geom, "vertex.radius", anari::newArray1D(d, radii.data(), radii.size()));
    }
    if (indexed) {
      std::vector<uint32_t> indices;
      for (uint32_t i = 0; i < vertices.size() / 2; i++)
        indices.push_back(i * 2);
      generator.shuffleVector(indices);
      if (opts.unusedVertices && indices.size() >= 2)
        indices.resize(indices.size() - 2);
      indiciCount = indices.size();
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  } else if (subtype == "cone") {
    auto [v, radii, caps] = generator.generateCones(primitiveCount, opts.vertexCaps);
    vertices = v;
    anari::setAndReleaseParameter(
        d, geom, "vertex.radius", anari::newArray1D(d, radii.data(), radii.size()));
    if (!caps.empty()) {
      anari::setAndReleaseParameter(
          d, geom, "vertex.cap", anari::newArray1D(d, caps.data(), caps.size()));
    }
    anari::setParameter(d, geom, "caps", opts.globalCaps);
    if (indexed) {
      std::vector<math::vec<uint32_t, 2>> indices;
      for (uint32_t i = 0; i < vertices.size(); i += 2)
        indices.emplace_back(i, i + 1);
      generator.shuffleVector(indices);
      if (opts.unusedVertices && !indices.empty())
        indices.resize(indices.size() - 1);
      indiciCount = indices.size();
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  } else if (subtype == "cylinder") {
    auto [v, radii, caps] =
        generator.generateCylinders(primitiveCount, opts.vertexCaps);
    vertices = v;
    if (opts.globalRadius.has_value()) {
      anari::setParameter(d, geom, "radius", opts.globalRadius.value());
    } else {
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.radius",
          anari::newArray1D(d, radii.data(), radii.size()));
    }
    if (!caps.empty()) {
      anari::setAndReleaseParameter(
          d, geom, "vertex.cap", anari::newArray1D(d, caps.data(), caps.size()));
    }
    anari::setParameter(d, geom, "caps", opts.globalCaps);
    if (indexed) {
      std::vector<math::vec<uint32_t, 2>> indices;
      for (uint32_t i = 0; i < vertices.size(); i += 2)
        indices.emplace_back(i, i + 1);
      generator.shuffleVector(indices);
      if (opts.unusedVertices && !indices.empty())
        indices.resize(indices.size() - 1);
      indiciCount = indices.size();
      anari::setAndReleaseParameter(d,
          geom,
          "primitive.index",
          anari::newArray1D(d, indices.data(), indices.size()));
    }
  }

  if (!opts.colorAttribute.empty()) {
    size_t colorCount = vertices.size();
    if (opts.colorAttribute.rfind("primitive", 0) == 0)
      colorCount = primitiveCount;
    auto attributeColor = scenes::colors::getColorVectorFromPalette(colorCount);
    anari::setAndReleaseParameter(d,
        geom,
        opts.colorAttribute.c_str(),
        anari::newArray1D(d, attributeColor.data(), attributeColor.size()));
  }

  if (!opts.opacityAttribute.empty()) {
    size_t opacityCount = vertices.size();
    if (opts.opacityAttribute.rfind("primitive", 0) == 0)
      opacityCount = primitiveCount;
    auto attributeOpacity =
        generator.generateAttributeFloat(opacityCount, 0.0f, 1.0f);
    anari::setAndReleaseParameter(d,
        geom,
        opts.opacityAttribute.c_str(),
        anari::newArray1D(d, attributeOpacity.data(), attributeOpacity.size()));
  }

  if (!vertices.empty()) {
    anari::setAndReleaseParameter(d,
        geom,
        "vertex.position",
        anari::newArray1D(d, vertices.data(), vertices.size()));
  }

  if (indiciCount == 0 && componentCount != 0)
    indiciCount = vertices.size() / componentCount;

  if (opts.vertexAttributes) {
    const auto f = generator.generateAttributeFloat(
        vertices.size(), opts.attributeMin, opts.attributeMax);
    anari::setAndReleaseParameter(
        d, geom, "vertex.attribute0", anari::newArray1D(d, f.data(), f.size()));
    const auto v2 = generator.generateAttributeVec2(
        vertices.size(), opts.attributeMin, opts.attributeMax);
    anari::setAndReleaseParameter(d,
        geom,
        "vertex.attribute1",
        anari::newArray1D(d, v2.data(), v2.size()));
    const auto v3 = generator.generateAttributeVec3(
        vertices.size(), opts.attributeMin, opts.attributeMax);
    anari::setAndReleaseParameter(d,
        geom,
        "vertex.attribute2",
        anari::newArray1D(d, v3.data(), v3.size()));
    const auto v4 = generator.generateAttributeVec4(
        vertices.size(), opts.attributeMin, opts.attributeMax);
    anari::setAndReleaseParameter(d,
        geom,
        "vertex.attribute3",
        anari::newArray1D(d, v4.data(), v4.size()));
  }

  if (opts.primitiveAttributes) {
    const auto f = generator.generateAttributeFloat(
        indiciCount, opts.attributeMin, opts.attributeMax);
    anari::setAndReleaseParameter(d,
        geom,
        "primitive.attribute0",
        anari::newArray1D(d, f.data(), f.size()));
    const auto v2 = generator.generateAttributeVec2(
        indiciCount, opts.attributeMin, opts.attributeMax);
    anari::setAndReleaseParameter(d,
        geom,
        "primitive.attribute1",
        anari::newArray1D(d, v2.data(), v2.size()));
    const auto v3 = generator.generateAttributeVec3(
        indiciCount, opts.attributeMin, opts.attributeMax);
    anari::setAndReleaseParameter(d,
        geom,
        "primitive.attribute2",
        anari::newArray1D(d, v3.data(), v3.size()));
    const auto v4 = generator.generateAttributeVec4(
        indiciCount, opts.attributeMin, opts.attributeMax);
    anari::setAndReleaseParameter(d,
        geom,
        "primitive.attribute3",
        anari::newArray1D(d, v4.data(), v4.size()));
  }

  anari::commitParameters(d, geom);
  return geom;
}

// --- Surfaces, materials, lights ---------------------------------------------

anari::Material makeMatteMaterial(anari::Device d, math::float3 color)
{
  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", color);
  anari::commitParameters(d, mat);
  return mat;
}

anari::Surface makeSurface(
    anari::Device d, anari::Geometry geometry, anari::Material material)
{
  auto surface = anari::newObject<anari::Surface>(d);
  anari::setParameter(d, surface, "geometry", geometry);
  anari::setParameter(d, surface, "material", material);
  anari::commitParameters(d, surface);
  return surface;
}

anari::Light makeDirectionalLight(
    anari::Device d, math::float3 direction, float irradiance)
{
  auto light = anari::newObject<anari::Light>(d, "directional");
  anari::setParameter(d, light, "direction", direction);
  anari::setParameter(d, light, "irradiance", irradiance);
  anari::commitParameters(d, light);
  return light;
}

anari::Light newLight(anari::Device d, const std::string &subtype)
{
  auto light = anari::newObject<anari::Light>(d, subtype.c_str());
  if (subtype == "hdri") {
    const size_t res = 64;
    auto hdr = scenes::TextureGenerator::generateCheckerBoardHDR(res);
    anari::setAndReleaseParameter(
        d, light, "radiance", anari::newArray2D(d, hdr.data(), res, res));
  }
  return light; // uncommitted: caller sets per-Case params, then commits
}

// --- Samplers ----------------------------------------------------------------

anari::Sampler makeCheckerboardSampler(anari::Device d, bool normalMap)
{
  auto sampler = anari::newObject<anari::Sampler>(d, "image2D");
  const size_t resolution = 64;
  auto image = normalMap
      ? scenes::TextureGenerator::generateCheckerBoardNormalMap(resolution)
      : scenes::TextureGenerator::generateCheckerBoard(resolution);
  anari::setAndReleaseParameter(d,
      sampler,
      "image",
      anari::newArray2D(d, image.data(), resolution, resolution));
  anari::commitParameters(d, sampler);
  return sampler;
}

anari::Sampler newImageSampler(anari::Device d,
    const std::string &subtype,
    const std::string &inAttribute,
    bool normalMap)
{
  auto sampler = anari::newObject<anari::Sampler>(d, subtype.c_str());

  if (subtype == "image1D") {
    auto img = scenes::TextureGenerator::generateGreyScale(16);
    anari::setAndReleaseParameter(
        d, sampler, "image", anari::newArray1D(d, img.data(), img.size()));
  } else if (subtype == "image2D") {
    const size_t res = 64;
    auto img = normalMap
        ? scenes::TextureGenerator::generateCheckerBoardNormalMap(res)
        : scenes::TextureGenerator::generateCheckerBoard(res);
    anari::setAndReleaseParameter(
        d, sampler, "image", anari::newArray2D(d, img.data(), res, res));
  } else if (subtype == "image3D") {
    const size_t res = 32;
    auto img = scenes::TextureGenerator::generateRGBRamp(res);
    anari::setAndReleaseParameter(
        d, sampler, "image", anari::newArray3D(d, img.data(), res, res, res));
  }

  if (!inAttribute.empty())
    anari::setParameter(d, sampler, "inAttribute", inAttribute.c_str());

  return sampler; // uncommitted: caller sets per-Case params, then commits
}

// --- Volumes -----------------------------------------------------------------

anari::SpatialField newStructuredRegularField(
    anari::Device d, std::array<uint32_t, 3> dimensions, int seed)
{
  auto field = anari::newObject<anari::SpatialField>(d, "structuredRegular");
  scenes::PrimitiveGenerator generator(seed);

  const size_t count = static_cast<size_t>(dimensions[0]) * dimensions[1]
      * dimensions[2];
  auto data = generator.generateAttributeFloat(count);

  anari::setParameter(d, field, "origin", math::float3(-1.f));
  anari::setParameter(d,
      field,
      "spacing",
      math::float3(dimensions[0] ? 2.f / dimensions[0] : 0.f,
          dimensions[1] ? 2.f / dimensions[1] : 0.f,
          dimensions[2] ? 2.f / dimensions[2] : 0.f));
  anari::setParameterArray3D(d,
      field,
      "data",
      data.data(),
      dimensions[0],
      dimensions[1],
      dimensions[2]);
  return field; // uncommitted: caller may override origin/spacing/filter
}

anari::SpatialField makeStructuredRegularField(
    anari::Device d, std::array<uint32_t, 3> dimensions, int seed)
{
  auto field = newStructuredRegularField(d, dimensions, seed);
  anari::commitParameters(d, field);
  return field;
}

anari::Volume makeVolume(anari::Device d, anari::SpatialField field)
{
  auto volume = anari::newObject<anari::Volume>(d, "transferFunction1D");
  anari::setParameter(d, volume, "value", field);

  std::vector<math::float3> colors = {
      {0.f, 0.f, 1.f}, {0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}};
  std::vector<float> opacities = {0.f, 1.f};
  anari::setAndReleaseParameter(
      d, volume, "color", anari::newArray1D(d, colors.data(), colors.size()));
  anari::setAndReleaseParameter(d,
      volume,
      "opacity",
      anari::newArray1D(d, opacities.data(), opacities.size()));
  anari::setParameter(d, volume, "valueRange", math::float2(0.f, 1.f));
  anari::commitParameters(d, volume);
  return volume;
}

// --- Renderer ----------------------------------------------------------------

anari::Renderer newRenderer(anari::Device d, const std::string &subtype)
{
  return anari::newObject<anari::Renderer>(d, subtype.c_str());
}

// --- Instances ---------------------------------------------------------------

anari::Instance makeInstance(anari::Device d,
    const std::vector<anari::Surface> &surfaces,
    math::mat4 transform)
{
  auto group = anari::newObject<anari::Group>(d);
  if (!surfaces.empty()) {
    anari::setAndReleaseParameter(d,
        group,
        "surface",
        anari::newArray1D(d, surfaces.data(), surfaces.size()));
  }
  anari::commitParameters(d, group);

  auto inst = anari::newObject<anari::Instance>(d, "transform");
  float xfm[16];
  std::memcpy(xfm, &transform, sizeof(xfm));
  anari::setParameter(d, inst, "transform", xfm);
  anari::setAndReleaseParameter(d, inst, "group", group);
  anari::commitParameters(d, inst);
  return inst;
}

// --- Cameras -----------------------------------------------------------------

scenes::Camera cameraFromBounds(const scenes::Bounds &bounds)
{
  const math::float3 size = bounds[1] - bounds[0];
  const math::float3 center = 0.5f * (bounds[0] + bounds[1]);
  const float distance = math::length(size);
  const math::float3 eye = center + math::float3(0.f, 0.f, distance);

  scenes::Camera cam;
  cam.position = eye;
  cam.direction = math::normalize(center - eye);
  cam.at = center;
  cam.up = math::float3(0.f, 1.f, 0.f);
  return cam;
}

anari::Camera makePerspectiveCamera(anari::Device d,
    const scenes::Camera &camera,
    const PerspectiveCameraOptions &opts)
{
  auto cam = anari::newObject<anari::Camera>(d, "perspective");
  anari::setParameter(d, cam, "position", camera.position);
  anari::setParameter(d, cam, "direction", camera.direction);
  anari::setParameter(d, cam, "up", camera.up);
  if (opts.fovy.has_value())
    anari::setParameter(d, cam, "fovy", opts.fovy.value());
  if (opts.aspect.has_value())
    anari::setParameter(d, cam, "aspect", opts.aspect.value());
  if (opts.near.has_value())
    anari::setParameter(d, cam, "near", opts.near.value());
  if (opts.far.has_value())
    anari::setParameter(d, cam, "far", opts.far.value());
  anari::commitParameters(d, cam);
  return cam;
}

// --- World assembly + framing ------------------------------------------------

anari::World assembleWorld(anari::Device d, const WorldContents &contents)
{
  auto world = anari::newObject<anari::World>(d);

  if (!contents.surfaces.empty()) {
    anari::setAndReleaseParameter(d,
        world,
        "surface",
        anari::newArray1D(
            d, contents.surfaces.data(), contents.surfaces.size()));
  }
  if (!contents.volumes.empty()) {
    anari::setAndReleaseParameter(d,
        world,
        "volume",
        anari::newArray1D(d, contents.volumes.data(), contents.volumes.size()));
  }
  if (!contents.lights.empty()) {
    anari::setAndReleaseParameter(d,
        world,
        "light",
        anari::newArray1D(d, contents.lights.data(), contents.lights.size()));
  }
  if (!contents.instances.empty()) {
    anari::setAndReleaseParameter(d,
        world,
        "instance",
        anari::newArray1D(
            d, contents.instances.data(), contents.instances.size()));
  }

  anari::commitParameters(d, world);
  return world;
}

scenes::Bounds worldBounds(anari::Device d, anari::World world)
{
  scenes::Bounds b = {math::float3(-1.f), math::float3(1.f)};
  anari::getProperty(d, world, "bounds", b);
  return b;
}

anari::Frame makeColorFrame(
    anari::Device d, anari::World world, uint32_t width, uint32_t height)
{
  const auto bounds = worldBounds(d, world);
  const auto camDesc = cameraFromBounds(bounds);
  PerspectiveCameraOptions camOpts;
  if (height != 0)
    camOpts.aspect = static_cast<float>(width) / static_cast<float>(height);
  auto camera = makePerspectiveCamera(d, camDesc, camOpts);

  auto renderer = anari::newObject<anari::Renderer>(d, "default");
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", math::vec<uint32_t, 2>(width, height));
  anari::setParameter(d, frame, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  anari::setParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "camera", camera);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  anari::release(d, renderer);
  anari::release(d, camera);
  return frame;
}

} // namespace cts
} // namespace anari
