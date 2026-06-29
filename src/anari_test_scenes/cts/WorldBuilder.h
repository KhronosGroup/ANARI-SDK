// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Value.h"
#include "anari_test_scenes.h"
#include "anari_test_scenes_export.h"
// anari
#include "anari/anari_cpp.hpp"
// std
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// Reusable CTS world-building helpers. Makers return committed handles owned by
// the caller.

namespace anari {
namespace cts {

// --- Generic axis-value parameter binding ------------------------------------

namespace detail {
// Apply an axis value to an object parameter, resolving "ref_sampler_N"
// references through the supplied sampler list. Invalid values leave it unset.
ANARI_TEST_SCENES_INTERFACE void setBoundParameterImpl(anari::Device d,
    anari::Object obj,
    const char *param,
    const Any &value,
    const std::vector<anari::Sampler> &samplers);
} // namespace detail

template <typename H>
inline void setBoundParameter(anari::Device d,
    H obj,
    const char *param,
    const Any &value,
    const std::vector<anari::Sampler> &samplers = {})
{
  detail::setBoundParameterImpl(
      d, reinterpret_cast<anari::Object>(obj), param, value, samplers);
}

// --- Geometry ----------------------------------------------------------------

// How a geometry is laid out. The primitives are placed as a deterministic,
// human-viewable Layout (ADR-0007): a near-square grid of identical canonical
// primitives, centered at the origin in the XY plane and facing +Z (the
// camera). No randomness is involved; the feature combinations a Test asserts
// live in these named fields.
struct GeometryOptions
{
  // triangle, quad, sphere, curve, cone, cylinder
  std::string subtype{"triangle"};
  // For triangle/quad subtypes: triangle, quad, cube.
  std::string shape{"triangle"};
  // soup or indexed.
  std::string primitiveMode{"soup"};
  int primitiveCount{1};

  // Cones/cylinders/spheres/curves: use one global radius instead of
  // per-vertex.
  std::optional<float> globalRadius;
  // Cones/cylinders: per-vertex caps flag (nullopt = leave unset).
  std::optional<int32_t> vertexCaps;
  // Cones/cylinders: global caps mode ("none", "first", "second", "both").
  std::string globalCaps{"none"};
  // Drop the last index so some vertices/primitives go unused (indexed only).
  bool unusedVertices{false};

  // Fill an attribute with palette colors (e.g. "vertex.color").
  std::string colorAttribute;
  // Fill an attribute with a left-to-right opacity ramp (e.g.
  // "vertex.attribute0").
  std::string opacityAttribute;

  // Fill the standard vertex/primitive attribute0..3 slots with deterministic
  // ramps (used by tests that assert attribute arrays exist, not their values).
  bool vertexAttributes{false};
  bool primitiveAttributes{false};
  float attributeMin{0.0f};
  float attributeMax{1.0f};
};

// Create, fill, and commit a geometry of the requested subtype. Returns the
// committed handle (caller owns it).
ANARI_TEST_SCENES_INTERFACE anari::Geometry buildGeometry(
    anari::Device d, const GeometryOptions &opts);

// The canonical equilateral-triangle Layout: three vertices per triangle in
// soup order, laid out on the near-square grid for `count` primitives. Shared
// by buildGeometry (triangle/triangle) and the material backdrop so both place
// their triangles identically.
ANARI_TEST_SCENES_INTERFACE std::vector<anari::math::float3> layoutTriangleSoup(
    int count);

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

// Create a light of the given subtype with no parameters set yet (caller sets
// per-Case parameters then commits). The `hdri` subtype additionally gets a
// synthetic radiance image, matching the old engine.
ANARI_TEST_SCENES_INTERFACE anari::Light newLight(
    anari::Device d, const std::string &subtype);

// --- Samplers ----------------------------------------------------------------

// A 2D image sampler holding a synthetic checkerboard (or its normal map).
ANARI_TEST_SCENES_INTERFACE anari::Sampler makeCheckerboardSampler(
    anari::Device d, bool normalMap = false);

// Create an image sampler with its synthetic image and `inAttribute` set, but
// NOT committed, so a Test can apply per-Case parameters (filter, wrapMode,
// transforms, offsets) and then commit. The image mirrors the old generators:
//   image1D -> 16-texel greyscale ramp
//   image2D -> 64x64 checkerboard, or its normal map when normalMap is true
//   image3D -> 32x32x32 RGB ramp
ANARI_TEST_SCENES_INTERFACE anari::Sampler newImageSampler(anari::Device d,
    const std::string &subtype,
    const std::string &inAttribute,
    bool normalMap = false);

// --- Volumes -----------------------------------------------------------------

// A scalar field sampled over the field's domain. The argument is a position in
// the centered, normalized domain [-1,1]^3; the return is the scalar stored at
// that grid point. Swap in a different function (e.g. a smooth sinusoid) to
// change the field's shape without touching the fill machinery (ADR-0007).
using FieldFn = std::function<float(anari::math::float3)>;

// Radial distance from the domain center: 0 at the center, growing to ~sqrt(3)
// at the corners. Isosurfaces at 0.25/0.5/0.75 are nested spheres fully inside
// the domain; a transfer-function volume shows a smooth radial gradient. This
// is the default field of (new|make)StructuredRegularField.
ANARI_TEST_SCENES_INTERFACE float radialDistanceField(anari::math::float3 p);

// A structuredRegular spatial field filled by sampling `field` over its domain.
ANARI_TEST_SCENES_INTERFACE anari::SpatialField makeStructuredRegularField(
    anari::Device d,
    std::array<uint32_t, 3> dimensions,
    const FieldFn &field = {});

// A structuredRegular spatial field with its analytic data array and default
// origin/spacing set, but NOT committed, so a Test can override origin/spacing/
// filter per Case and then commit. A null `field` defaults to
// radialDistanceField.
ANARI_TEST_SCENES_INTERFACE anari::SpatialField newStructuredRegularField(
    anari::Device d,
    std::array<uint32_t, 3> dimensions,
    const FieldFn &field = {});

// A transferFunction1D volume over the given spatial field.
ANARI_TEST_SCENES_INTERFACE anari::Volume makeVolume(
    anari::Device d, anari::SpatialField field);

// --- Renderer ----------------------------------------------------------------

// Create a renderer of the given subtype with no parameters set yet (caller
// sets per-Case parameters then commits). Used by the renderer build hook.
ANARI_TEST_SCENES_INTERFACE anari::Renderer newRenderer(
    anari::Device d, const std::string &subtype = "default");

// --- Instances ---------------------------------------------------------------

// A group over the given surfaces, wrapped in an instance with a transform.
ANARI_TEST_SCENES_INTERFACE anari::Instance makeInstance(anari::Device d,
    const std::vector<anari::Surface> &surfaces,
    anari::math::mat4 transform = anari::math::identity);

// --- Cameras -----------------------------------------------------------------

// Frame a bounding box from +Z, looking at its center, with the subject filling
// most of the frame (assumes the reference 60° fovy). Pure: no device needed.
ANARI_TEST_SCENES_INTERFACE anari::scenes::Camera cameraFromBounds(
    const anari::scenes::Bounds &bounds);

// Optional perspective-camera parameters. Each is set on the camera only when
// present, so a Test can vary just the ones it cares about (the camera category
// permutes fovy/aspect/near/far). Note: the helide reference device implements
// fovy/aspect and ignores near/far.
struct PerspectiveCameraOptions
{
  std::optional<float> fovy; // radians
  std::optional<float> aspect;
  std::optional<float> near;
  std::optional<float> far;
};

// A perspective camera placed per a Camera description, with optional
// intrinsics.
ANARI_TEST_SCENES_INTERFACE anari::Camera makePerspectiveCamera(anari::Device d,
    const anari::scenes::Camera &camera,
    const PerspectiveCameraOptions &opts = {});

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
ANARI_TEST_SCENES_INTERFACE anari::Frame makeColorFrame(
    anari::Device d, anari::World world, uint32_t width, uint32_t height);

} // namespace cts
} // namespace anari
