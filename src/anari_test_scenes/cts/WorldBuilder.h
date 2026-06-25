// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari_test_scenes.h"
#include "anari_test_scenes_export.h"
// anari
#include "anari/anari_cpp.hpp"
// std
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Reusable world-building helpers extracted from the old generic
// SceneGenerator::commit(). Tests author their ANARI world by calling these
// free functions with normal ANARI C++ calls; the harness owns the resulting
// World. Each maker returns a committed handle owned by the caller (release it,
// or hand it to the World which retains it). These build on PrimitiveGenerator,
// TextureGenerator, and ColorPalette.

namespace anari {
namespace cts {

// --- Geometry ----------------------------------------------------------------

// How a geometry is generated. Mirrors the parameters the old SceneGenerator
// drove from JSON.
struct GeometryOptions
{
  // triangle, quad, sphere, curve, cone, cylinder
  std::string subtype{"triangle"};
  // For triangle/quad subtypes: triangle, quad, cube.
  std::string shape{"triangle"};
  // soup or indexed.
  std::string primitiveMode{"soup"};
  int primitiveCount{1};
  int seed{0};

  // Cones/cylinders/spheres/curves: use one global radius instead of per-vertex.
  std::optional<float> globalRadius;
  // Cones/cylinders: per-vertex caps flag (nullopt = leave unset).
  std::optional<int32_t> vertexCaps;
  // Cones/cylinders: global caps mode ("none", "first", "second", "both").
  std::string globalCaps{"none"};
  // Drop the last index so some vertices/primitives go unused (indexed only).
  bool unusedVertices{false};

  // Fill an attribute with palette colors (e.g. "vertex.color").
  std::string colorAttribute;
  // Fill an attribute with opacity values (e.g. "vertex.attribute0").
  std::string opacityAttribute;

  // Fill the standard vertex/primitive attribute0..3 slots with random data.
  bool vertexAttributes{false};
  bool primitiveAttributes{false};
  float attributeMin{0.0f};
  float attributeMax{1.0f};
};

// Create, fill, and commit a geometry of the requested subtype. Returns the
// committed handle (caller owns it).
ANARI_TEST_SCENES_INTERFACE anari::Geometry buildGeometry(
    anari::Device d, const GeometryOptions &opts);

// --- Surfaces, materials, lights ---------------------------------------------

// A matte material with a constant base color.
ANARI_TEST_SCENES_INTERFACE anari::Material makeMatteMaterial(
    anari::Device d, anari::math::float3 color);

// A surface binding a geometry to a material (both committed already).
ANARI_TEST_SCENES_INTERFACE anari::Surface makeSurface(
    anari::Device d, anari::Geometry geometry, anari::Material material);

// A directional light.
ANARI_TEST_SCENES_INTERFACE anari::Light makeDirectionalLight(
    anari::Device d, anari::math::float3 direction, float irradiance = 4.0f);

// --- Samplers ----------------------------------------------------------------

// A 2D image sampler holding a synthetic checkerboard (or its normal map).
ANARI_TEST_SCENES_INTERFACE anari::Sampler makeCheckerboardSampler(
    anari::Device d, bool normalMap = false);

// --- Volumes -----------------------------------------------------------------

// A structuredRegular spatial field filled with random data of the given
// dimensions.
ANARI_TEST_SCENES_INTERFACE anari::SpatialField makeStructuredRegularField(
    anari::Device d, std::array<uint32_t, 3> dimensions, int seed = 0);

// A transferFunction1D volume over the given spatial field.
ANARI_TEST_SCENES_INTERFACE anari::Volume makeVolume(
    anari::Device d, anari::SpatialField field);

// --- Instances ---------------------------------------------------------------

// A group over the given surfaces, wrapped in an instance with a transform.
ANARI_TEST_SCENES_INTERFACE anari::Instance makeInstance(anari::Device d,
    const std::vector<anari::Surface> &surfaces,
    anari::math::mat4 transform = anari::math::identity);

// --- Cameras -----------------------------------------------------------------

// Frame a bounding box from +Z, looking at its center. Pure: no device needed.
ANARI_TEST_SCENES_INTERFACE anari::scenes::Camera cameraFromBounds(
    const anari::scenes::Bounds &bounds);

// A perspective camera placed per a Camera description.
ANARI_TEST_SCENES_INTERFACE anari::Camera makePerspectiveCamera(
    anari::Device d,
    const anari::scenes::Camera &camera,
    std::optional<float> aspect = std::nullopt);

// --- World assembly + framing ------------------------------------------------

// What goes into a World. Any list may be empty.
struct WorldContents
{
  std::vector<anari::Surface> surfaces;
  std::vector<anari::Volume> volumes;
  std::vector<anari::Light> lights;
  std::vector<anari::Instance> instances;
};

// Assemble and commit a World from its contents (caller owns the World).
ANARI_TEST_SCENES_INTERFACE anari::World assembleWorld(
    anari::Device d, const WorldContents &contents);

// World-space bounds of a committed world.
ANARI_TEST_SCENES_INTERFACE anari::scenes::Bounds worldBounds(
    anari::Device d, anari::World world);

// A committed frame with a default renderer, a color channel, and a camera
// framing the world's bounds. Convenience for smoke tests / simple renders.
ANARI_TEST_SCENES_INTERFACE anari::Frame makeColorFrame(anari::Device d,
    anari::World world,
    uint32_t width,
    uint32_t height);

} // namespace cts
} // namespace anari
