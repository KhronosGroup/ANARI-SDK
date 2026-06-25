// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Geometry conformance tests, migrated from cts/test_scenes/geometry/**.
// Each builds a single-surface, lit world from one geometry subtype, varying
// the parameters the old JSON scenes exercised: soup-vs-indexed equivalence
// (a Variant), primitive count, caps, global radius, unused vertices, vertex/
// primitive color and attribute arrays, normals/tangents, and frame color
// output types.

#include "Categories.h"
#include "../BuildContext.h"
#include "../TestBuilder.h"
#include "../WorldBuilder.h"
#include "generators/ColorPalette.h"
// std
#include <string>
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float3;
using anari::math::float4;

const std::string kMatte = "ANARI_KHR_MATERIAL_MATTE";

// Overlay the per-Case axis values a geometry Test may carry onto base options.
GeometryOptions withAxes(BuildContext &ctx, GeometryOptions o)
{
  o.primitiveMode = ctx.getString("primitiveMode", o.primitiveMode);
  o.primitiveCount = ctx.get<int>("primitiveCount", o.primitiveCount);
  o.globalCaps = ctx.getString("globalCaps", o.globalCaps);
  o.colorAttribute = ctx.getString("color", o.colorAttribute);
  if (ctx.has("vertexCaps"))
    o.vertexCaps = ctx.get<bool>("vertexCaps", true) ? 1 : 0;
  return o;
}

// One-surface, lit world. The matte material is bound to the geometry's "color"
// attribute when opts.colorAttribute is set (the *_colors tests), else a
// constant color.
anari::World surfaceWorld(BuildContext &ctx, const GeometryOptions &opts)
{
  auto d = ctx.device();
  auto geom = buildGeometry(d, opts);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  if (!opts.colorAttribute.empty())
    anari::setParameter(d, mat, "color", "color");
  else
    anari::setParameter(d, mat, "color", float3(0.7f, 0.5f, 0.3f));
  anari::commitParameters(d, mat);

  auto surface = makeSurface(d, geom, mat);
  auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));

  WorldContents contents;
  contents.surfaces = {surface};
  contents.lights = {light};
  auto world = assembleWorld(d, contents);

  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  anari::release(d, light);
  return world;
}

// Build options for a basic shape Test of the given subtype/shape.
GeometryOptions base(const char *subtype, const char *shape, int count)
{
  GeometryOptions o;
  o.subtype = subtype;
  o.shape = shape;
  o.primitiveCount = count;
  return o;
}

// A single-primitive world with explicit normals/tangents (the *_normal_tangent
// tests). normalMode in {set, unset}; tangentMode in {unset, vec3, vec4}. Matte
// ignores them, so all combinations must render identically (Variant axes).
anari::World normalTangentWorld(BuildContext &ctx, bool quad)
{
  auto d = ctx.device();
  std::vector<float3> pos;
  if (quad)
    pos = {{0.1f, 0.4f, 0.6f},
        {0.6f, 0.4f, 0.6f},
        {0.6f, 0.7f, 0.7f},
        {0.1f, 0.7f, 0.7f}};
  else
    pos = {{0.1f, 0.4f, 0.6f}, {0.6f, 0.4f, 0.6f}, {0.35f, 0.7f, 0.8f}};

  auto geom = anari::newObject<anari::Geometry>(d, quad ? "quad" : "triangle");
  anari::setAndReleaseParameter(
      d, geom, "vertex.position", anari::newArray1D(d, pos.data(), pos.size()));
  auto cols = scenes::colors::getColorVectorFromPalette(pos.size());
  anari::setAndReleaseParameter(
      d, geom, "vertex.color", anari::newArray1D(d, cols.data(), cols.size()));

  if (ctx.getString("normal", "set") == "set") {
    std::vector<float3> n(pos.size(), float3(0.f, 0.f, 1.f));
    n[0] = float3(1.f, 0.f, 0.f);
    n[1] = float3(0.f, 1.f, 0.f);
    anari::setAndReleaseParameter(
        d, geom, "vertex.normal", anari::newArray1D(d, n.data(), n.size()));
  }
  const std::string tangent = ctx.getString("tangent", "unset");
  if (tangent == "vec3") {
    std::vector<float3> t(pos.size(), float3(0.f, 1.f, 0.f));
    t[1] = float3(1.f, 0.f, 0.f);
    anari::setAndReleaseParameter(
        d, geom, "vertex.tangent", anari::newArray1D(d, t.data(), t.size()));
  } else if (tangent == "vec4") {
    std::vector<float4> t(pos.size(), float4(0.f, 1.f, 0.f, -1.f));
    t[1] = float4(1.f, 0.f, 0.f, -1.f);
    anari::setAndReleaseParameter(
        d, geom, "vertex.tangent", anari::newArray1D(d, t.data(), t.size()));
  }
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", "color");
  anari::commitParameters(d, mat);
  auto surface = makeSurface(d, geom, mat);
  auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));
  WorldContents contents;
  contents.surfaces = {surface};
  contents.lights = {light};
  auto world = assembleWorld(d, contents);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  anari::release(d, light);
  return world;
}

anari::World isosurfaceWorld(BuildContext &ctx)
{
  auto d = ctx.device();
  auto field = makeStructuredRegularField(d, {16, 16, 16});
  auto geom = anari::newObject<anari::Geometry>(d, "isosurface");
  anari::setParameter(d, geom, "field", field);
  std::vector<float> iso = {0.25f, 0.5f, 0.75f};
  anari::setAndReleaseParameter(
      d, geom, "isovalue", anari::newArray1D(d, iso.data(), iso.size()));
  anari::commitParameters(d, geom);

  auto mat = makeMatteMaterial(d, float3(0.7f, 0.5f, 0.3f));
  auto surface = makeSurface(d, geom, mat);
  auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));
  WorldContents contents;
  contents.surfaces = {surface};
  contents.lights = {light};
  auto world = assembleWorld(d, contents);
  anari::release(d, field);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  anari::release(d, light);
  return world;
}

const std::vector<Channel> kColorDepth = {Channel::Color, Channel::Depth};
const std::vector<Channel> kColorOnly = {Channel::Color};

const std::vector<Any> kFrameColorTypes = {
    Any("UFIXED8_VEC4"), Any("UFIXED8_RGBA_SRGB"), Any("FLOAT32_VEC4")};

// Register the family of tests shared by every "shaped" geometry subtype
// (triangle/quad and the sphere-like subtypes differ only in available extras).
void registerShape(Catalog &catalog,
    const char *prefix,
    const char *subtype,
    const char *shape,
    const char *feature,
    bool soupVariantOnly)
{
  // Basic soup-vs-indexed equivalence: a Variant sharing one ground truth.
  auto basic = makeTest("geometry", prefix);
  basic.build([=](BuildContext &ctx) {
    return surfaceWorld(ctx, withAxes(ctx, base(subtype, shape, 16)));
  });
  if (soupVariantOnly)
    basic.variant("primitiveMode", {"soup", "indexed"});
  else
    basic.permute("primitiveMode", {"soup", "indexed"});
  basic.channels(kColorDepth)
      .requireFeatures({feature, kMatte})
      .registerInto(catalog);
}

} // namespace

void registerGeometryTests(Catalog &catalog)
{
  using V = std::vector<Any>;

  // ---- triangle -------------------------------------------------------------
  registerShape(catalog, "triangle", "triangle", "triangle",
      "ANARI_KHR_GEOMETRY_TRIANGLE", /*soupVariantOnly=*/true);
  registerShape(catalog, "triangle_quad", "triangle", "quad",
      "ANARI_KHR_GEOMETRY_TRIANGLE", true);
  registerShape(catalog, "triangle_cube", "triangle", "cube",
      "ANARI_KHR_GEOMETRY_TRIANGLE", true);

  makeTest("geometry", "triangle_colors")
      .build([](BuildContext &ctx) {
        return surfaceWorld(
            ctx, withAxes(ctx, base("triangle", "triangle", 4)));
      })
      .permute("color", {"primitive.color", "vertex.color"})
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_TRIANGLE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "triangle_normal_tangent")
      .build([](BuildContext &ctx) { return normalTangentWorld(ctx, false); })
      .variant("normal", {"set", "unset"})
      .variant("tangent", {"unset", "vec3", "vec4"})
      .simplified()
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_TRIANGLE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "triangle_random_attributes")
      .build([](BuildContext &ctx) {
        GeometryOptions o = base("triangle", "triangle", 16);
        o.vertexAttributes = true;
        o.primitiveAttributes = true;
        return surfaceWorld(ctx, withAxes(ctx, o));
      })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_TRIANGLE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "triangle_unused_vertices")
      .build([](BuildContext &ctx) {
        GeometryOptions o = base("triangle", "triangle", 4);
        o.primitiveMode = "indexed";
        o.unusedVertices = true;
        return surfaceWorld(ctx, withAxes(ctx, o));
      })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_TRIANGLE", kMatte})
      .registerInto(catalog);

  // ---- quad -----------------------------------------------------------------
  registerShape(catalog, "quad", "quad", "quad", "ANARI_KHR_GEOMETRY_QUAD", true);
  registerShape(catalog, "quad_cube", "quad", "cube",
      "ANARI_KHR_GEOMETRY_QUAD", true);

  makeTest("geometry", "quad_colors")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, base("quad", "quad", 4)));
      })
      .permute("color", {"primitive.color", "vertex.color"})
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_QUAD", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "quad_frame_color_types")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, base("quad", "quad", 16)));
      })
      .permute("frame_color_type", kFrameColorTypes)
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_QUAD", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "quad_normal_tangent")
      .build([](BuildContext &ctx) { return normalTangentWorld(ctx, true); })
      .variant("normal", {"set", "unset"})
      .variant("tangent", {"unset", "vec3", "vec4"})
      .simplified()
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_QUAD", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "quad_unused_vertices")
      .build([](BuildContext &ctx) {
        GeometryOptions o = base("quad", "quad", 4);
        o.primitiveMode = "indexed";
        o.unusedVertices = true;
        return surfaceWorld(ctx, withAxes(ctx, o));
      })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_QUAD", kMatte})
      .registerInto(catalog);

  // ---- sphere ---------------------------------------------------------------
  makeTest("geometry", "sphere")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, base("sphere", "", 16)));
      })
      .permute("primitiveCount", {1, 16})
      .variant("primitiveMode", {"soup", "indexed"})
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_SPHERE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "sphere_colors")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, base("sphere", "", 4)));
      })
      .permute("color", {"primitive.color", "vertex.color"})
      .permute("primitiveMode", {"soup", "indexed"})
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_SPHERE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "sphere_frame_color_types")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, base("sphere", "", 16)));
      })
      .permute("frame_color_type", kFrameColorTypes)
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_SPHERE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "sphere_global_radii")
      .build([](BuildContext &ctx) {
        GeometryOptions o = base("sphere", "", 16);
        o.globalRadius = 0.05f;
        return surfaceWorld(ctx, withAxes(ctx, o));
      })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_SPHERE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "sphere_unused_vertices")
      .build([](BuildContext &ctx) {
        GeometryOptions o = base("sphere", "", 4);
        o.primitiveMode = "indexed";
        o.unusedVertices = true;
        return surfaceWorld(ctx, withAxes(ctx, o));
      })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_SPHERE", kMatte})
      .registerInto(catalog);

  // ---- cone -----------------------------------------------------------------
  registerShape(catalog, "cone", "cone", "", "ANARI_KHR_GEOMETRY_CONE", true);

  makeTest("geometry", "cone_colors")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, base("cone", "", 4)));
      })
      .permute("color", {"primitive.color", "vertex.color"})
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CONE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "cone_frame_color_types")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, base("cone", "", 16)));
      })
      .permute("frame_color_type", kFrameColorTypes)
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CONE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "cone_global_caps")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, base("cone", "", 16)));
      })
      .permute("globalCaps", {"none", "first", "second", "both"})
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CONE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "cone_vertex_caps")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, base("cone", "", 16)));
      })
      .permute("vertexCaps", V{Any(true), Any(false)})
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CONE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "cone_unused_vertices")
      .build([](BuildContext &ctx) {
        GeometryOptions o = base("cone", "", 4);
        o.primitiveMode = "indexed";
        o.unusedVertices = true;
        return surfaceWorld(ctx, withAxes(ctx, o));
      })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CONE", kMatte})
      .registerInto(catalog);

  // ---- cylinder -------------------------------------------------------------
  registerShape(catalog, "cylinder", "cylinder", "",
      "ANARI_KHR_GEOMETRY_CYLINDER", true);

  makeTest("geometry", "cylinder_colors")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, base("cylinder", "", 4)));
      })
      .permute("color", {"primitive.color", "vertex.color"})
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CYLINDER", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "cylinder_frame_color_types")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, base("cylinder", "", 16)));
      })
      .permute("frame_color_type", kFrameColorTypes)
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CYLINDER", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "cylinder_global_caps")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, base("cylinder", "", 16)));
      })
      .permute("globalCaps", {"none", "first", "second", "both"})
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CYLINDER", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "cylinder_global_radii")
      .build([](BuildContext &ctx) {
        GeometryOptions o = base("cylinder", "", 16);
        o.globalRadius = 0.05f;
        return surfaceWorld(ctx, withAxes(ctx, o));
      })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CYLINDER", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "cylinder_vertex_caps")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, base("cylinder", "", 16)));
      })
      .permute("vertexCaps", V{Any(true), Any(false)})
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CYLINDER", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "cylinder_unused_vertices")
      .build([](BuildContext &ctx) {
        GeometryOptions o = base("cylinder", "", 4);
        o.primitiveMode = "indexed";
        o.unusedVertices = true;
        return surfaceWorld(ctx, withAxes(ctx, o));
      })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CYLINDER", kMatte})
      .registerInto(catalog);

  // ---- curve ----------------------------------------------------------------
  registerShape(catalog, "curve", "curve", "", "ANARI_KHR_GEOMETRY_CURVE",
      /*soupVariantOnly=*/false);

  makeTest("geometry", "curve_colors")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, base("curve", "", 4)));
      })
      .permute("color", {"primitive.color", "vertex.color"})
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CURVE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "curve_frame_color_types")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, base("curve", "", 16)));
      })
      .permute("frame_color_type", kFrameColorTypes)
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CURVE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "curve_global_radii")
      .build([](BuildContext &ctx) {
        GeometryOptions o = base("curve", "", 16);
        o.globalRadius = 0.05f;
        return surfaceWorld(ctx, withAxes(ctx, o));
      })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CURVE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "curve_unused_vertices")
      .build([](BuildContext &ctx) {
        GeometryOptions o = base("curve", "", 4);
        o.primitiveMode = "indexed";
        o.unusedVertices = true;
        return surfaceWorld(ctx, withAxes(ctx, o));
      })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CURVE", kMatte})
      .registerInto(catalog);

  // ---- isosurface -----------------------------------------------------------
  makeTest("geometry", "isosurface")
      .build([](BuildContext &ctx) { return isosurfaceWorld(ctx); })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR",
          "ANARI_KHR_GEOMETRY_ISOSURFACE",
          kMatte})
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
