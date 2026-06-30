// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "../BuildContext.h"
#include "../GeometryBuilder.h"
#include "../LightBuilder.h"
#include "../SurfaceBuilder.h"
#include "../TestBuilder.h"
#include "../VolumeBuilder.h"
#include "../WorldBuilder.h"
#include "Categories.h"
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

template <typename Spec>
void applyCommonAxes(BuildContext &ctx, Spec &spec)
{
  if (ctx.has("primitiveMode"))
    spec.mode = parsePrimitiveMode(ctx.getString("primitiveMode", ""));
  spec.primitiveCount = ctx.get<int>("primitiveCount", spec.primitiveCount);
  if (ctx.has("color"))
    spec.attributes.color = parseColorAttribute(ctx.getString("color", ""));
}

template <typename Spec>
Spec withAxes(BuildContext &ctx, Spec spec)
{
  applyCommonAxes(ctx, spec);
  return spec;
}

ConeSpec withAxes(BuildContext &ctx, ConeSpec spec)
{
  applyCommonAxes(ctx, spec);
  if (ctx.has("globalCaps"))
    spec.caps = parseCapsMode(ctx.getString("globalCaps", ""));
  if (ctx.has("vertexCaps"))
    spec.vertexCaps = ctx.get<bool>("vertexCaps", true);
  return spec;
}

CylinderSpec withAxes(BuildContext &ctx, CylinderSpec spec)
{
  applyCommonAxes(ctx, spec);
  if (ctx.has("globalCaps"))
    spec.caps = parseCapsMode(ctx.getString("globalCaps", ""));
  if (ctx.has("vertexCaps"))
    spec.vertexCaps = ctx.get<bool>("vertexCaps", true);
  return spec;
}

anari::Geometry publishGeometry(anari::Device d, const TriangleSpec &spec)
{
  return buildTriangleGeometry(d, spec);
}

anari::Geometry publishGeometry(anari::Device d, const QuadSpec &spec)
{
  return buildQuadGeometry(d, spec);
}

anari::Geometry publishGeometry(anari::Device d, const SphereSpec &spec)
{
  return buildSphereGeometry(d, spec);
}

anari::Geometry publishGeometry(anari::Device d, const CurveSpec &spec)
{
  return buildCurveGeometry(d, spec);
}

anari::Geometry publishGeometry(anari::Device d, const ConeSpec &spec)
{
  return buildConeGeometry(d, spec);
}

anari::Geometry publishGeometry(anari::Device d, const CylinderSpec &spec)
{
  return buildCylinderGeometry(d, spec);
}

template <typename Spec>
anari::World surfaceWorld(BuildContext &ctx, const Spec &spec)
{
  auto d = ctx.device();
  auto geom = publishGeometry(d, spec);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  if (spec.attributes.color != ColorAttribute::None)
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

TriangleSpec triangle(TriangleShape shape, int count)
{
  TriangleSpec spec;
  spec.shape = shape;
  spec.primitiveCount = count;
  return spec;
}

QuadSpec quad(QuadShape shape, int count)
{
  QuadSpec spec;
  spec.shape = shape;
  spec.primitiveCount = count;
  return spec;
}

SphereSpec sphere(int count)
{
  SphereSpec spec;
  spec.primitiveCount = count;
  return spec;
}

ConeSpec cone(int count)
{
  ConeSpec spec;
  spec.primitiveCount = count;
  return spec;
}

CylinderSpec cylinder(int count)
{
  CylinderSpec spec;
  spec.primitiveCount = count;
  return spec;
}

CurveSpec curve(int count)
{
  CurveSpec spec;
  spec.primitiveCount = count;
  return spec;
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
template <typename Spec>
void registerShape(Catalog &catalog,
    const char *prefix,
    Spec spec,
    const char *description,
    const char *feature,
    bool soupVariantOnly)
{
  // Basic soup-vs-indexed equivalence: a Variant sharing one ground truth.
  auto basic = makeTest("geometry", prefix);
  basic.description(description);
  basic.build([=](BuildContext &ctx) {
    return surfaceWorld(ctx, withAxes(ctx, spec));
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
  registerShape(catalog,
      "triangle",
      triangle(TriangleShape::Triangle, 16),
      "Checks basic triangle geometry in soup and indexed modes.",
      "ANARI_KHR_GEOMETRY_TRIANGLE",
      /*soupVariantOnly=*/true);
  registerShape(catalog,
      "triangle_quad",
      triangle(TriangleShape::Quad, 16),
      "Checks triangles arranged as a quad in soup and indexed modes.",
      "ANARI_KHR_GEOMETRY_TRIANGLE",
      true);
  registerShape(catalog,
      "triangle_cube",
      triangle(TriangleShape::Cube, 16),
      "Checks triangles arranged as a cube in soup and indexed modes.",
      "ANARI_KHR_GEOMETRY_TRIANGLE",
      true);

  makeTest("geometry", "triangle_colors")
      .description("Checks primitive and vertex colors on triangle geometry.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(
            ctx, withAxes(ctx, triangle(TriangleShape::Triangle, 4)));
      })
      .permute("color", {"primitive.color", "vertex.color"})
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_TRIANGLE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "triangle_normal_tangent")
      .description(
          "Checks optional triangle normal and tangent attribute layouts.")
      .build([](BuildContext &ctx) { return normalTangentWorld(ctx, false); })
      .variant("normal", {"set", "unset"})
      .variant("tangent", {"unset", "vec3", "vec4"})
      .simplified()
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_TRIANGLE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "triangle_random_attributes")
      .description(
          "Checks triangle vertex and primitive attributes with varied values.")
      .build([](BuildContext &ctx) {
        auto spec = triangle(TriangleShape::Triangle, 16);
        spec.attributes.vertexRamps = true;
        spec.attributes.primitiveRamps = true;
        return surfaceWorld(ctx, withAxes(ctx, spec));
      })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_TRIANGLE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "triangle_unused_vertices")
      .description(
          "Checks indexed triangles that contain unreferenced vertices.")
      .build([](BuildContext &ctx) {
        auto spec = triangle(TriangleShape::Triangle, 4);
        spec.mode = PrimitiveMode::Indexed;
        spec.unusedVertices = true;
        return surfaceWorld(ctx, withAxes(ctx, spec));
      })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_TRIANGLE", kMatte})
      .registerInto(catalog);

  // ---- quad -----------------------------------------------------------------
  registerShape(catalog,
      "quad",
      quad(QuadShape::Quad, 16),
      "Checks basic quad geometry in soup and indexed modes.",
      "ANARI_KHR_GEOMETRY_QUAD",
      true);
  registerShape(catalog,
      "quad_cube",
      quad(QuadShape::Cube, 16),
      "Checks quads arranged as a cube in soup and indexed modes.",
      "ANARI_KHR_GEOMETRY_QUAD",
      true);

  makeTest("geometry", "quad_colors")
      .description("Checks primitive and vertex colors on quad geometry.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, quad(QuadShape::Quad, 4)));
      })
      .permute("color", {"primitive.color", "vertex.color"})
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_QUAD", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "quad_frame_color_types")
      .description("Checks quad rendering across supported color buffer types.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, quad(QuadShape::Quad, 16)));
      })
      .permute("frame_color_type", kFrameColorTypes)
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_QUAD", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "quad_normal_tangent")
      .description("Checks optional quad normal and tangent attribute layouts.")
      .build([](BuildContext &ctx) { return normalTangentWorld(ctx, true); })
      .variant("normal", {"set", "unset"})
      .variant("tangent", {"unset", "vec3", "vec4"})
      .simplified()
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_QUAD", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "quad_unused_vertices")
      .description("Checks indexed quads that contain unreferenced vertices.")
      .build([](BuildContext &ctx) {
        auto spec = quad(QuadShape::Quad, 4);
        spec.mode = PrimitiveMode::Indexed;
        spec.unusedVertices = true;
        return surfaceWorld(ctx, withAxes(ctx, spec));
      })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_QUAD", kMatte})
      .registerInto(catalog);

  // ---- sphere ---------------------------------------------------------------
  makeTest("geometry", "sphere")
      .description(
          "Checks sphere counts and equivalent soup and indexed layouts.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, sphere(16)));
      })
      .permute("primitiveCount", {1, 16})
      .variant("primitiveMode", {"soup", "indexed"})
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_SPHERE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "sphere_colors")
      .description("Checks primitive and vertex colors on sphere geometry.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, sphere(4)));
      })
      .permute("color", {"primitive.color", "vertex.color"})
      .permute("primitiveMode", {"soup", "indexed"})
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_SPHERE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "sphere_frame_color_types")
      .description(
          "Checks sphere rendering across supported color buffer types.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, sphere(16)));
      })
      .permute("frame_color_type", kFrameColorTypes)
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_SPHERE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "sphere_global_radii")
      .description("Checks the global radius parameter on sphere geometry.")
      .build([](BuildContext &ctx) {
        auto spec = sphere(16);
        spec.globalRadius = 0.05f;
        return surfaceWorld(ctx, withAxes(ctx, spec));
      })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_SPHERE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "sphere_unused_vertices")
      .description("Checks indexed spheres that contain unreferenced vertices.")
      .build([](BuildContext &ctx) {
        auto spec = sphere(4);
        spec.mode = PrimitiveMode::Indexed;
        spec.unusedVertices = true;
        return surfaceWorld(ctx, withAxes(ctx, spec));
      })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_SPHERE", kMatte})
      .registerInto(catalog);

  // ---- cone -----------------------------------------------------------------
  registerShape(catalog,
      "cone",
      cone(16),
      "Checks basic cone geometry in soup and indexed modes.",
      "ANARI_KHR_GEOMETRY_CONE",
      true);

  makeTest("geometry", "cone_colors")
      .description("Checks primitive and vertex colors on cone geometry.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, cone(4)));
      })
      .permute("color", {"primitive.color", "vertex.color"})
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CONE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "cone_frame_color_types")
      .description("Checks cone rendering across supported color buffer types.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, cone(16)));
      })
      .permute("frame_color_type", kFrameColorTypes)
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CONE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "cone_global_caps")
      .description("Checks global end-cap modes on cone geometry.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, cone(16)));
      })
      .permute("globalCaps", {"none", "first", "second", "both"})
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CONE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "cone_vertex_caps")
      .description("Checks per-vertex end-cap control on cone geometry.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, cone(16)));
      })
      .permute("vertexCaps", V{Any(true), Any(false)})
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CONE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "cone_unused_vertices")
      .description("Checks indexed cones that contain unreferenced vertices.")
      .build([](BuildContext &ctx) {
        auto spec = cone(4);
        spec.mode = PrimitiveMode::Indexed;
        spec.unusedVertices = true;
        return surfaceWorld(ctx, withAxes(ctx, spec));
      })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CONE", kMatte})
      .registerInto(catalog);

  // ---- cylinder -------------------------------------------------------------
  registerShape(catalog,
      "cylinder",
      cylinder(16),
      "Checks basic cylinder geometry in soup and indexed modes.",
      "ANARI_KHR_GEOMETRY_CYLINDER",
      true);

  makeTest("geometry", "cylinder_colors")
      .description("Checks primitive and vertex colors on cylinder geometry.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, cylinder(4)));
      })
      .permute("color", {"primitive.color", "vertex.color"})
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CYLINDER", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "cylinder_frame_color_types")
      .description(
          "Checks cylinder rendering across supported color buffer types.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, cylinder(16)));
      })
      .permute("frame_color_type", kFrameColorTypes)
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CYLINDER", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "cylinder_global_caps")
      .description("Checks global end-cap modes on cylinder geometry.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, cylinder(16)));
      })
      .permute("globalCaps", {"none", "first", "second", "both"})
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CYLINDER", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "cylinder_global_radii")
      .description("Checks the global radius parameter on cylinder geometry.")
      .build([](BuildContext &ctx) {
        auto spec = cylinder(16);
        spec.globalRadius = 0.05f;
        return surfaceWorld(ctx, withAxes(ctx, spec));
      })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CYLINDER", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "cylinder_vertex_caps")
      .description("Checks per-vertex end-cap control on cylinder geometry.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, cylinder(16)));
      })
      .permute("vertexCaps", V{Any(true), Any(false)})
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CYLINDER", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "cylinder_unused_vertices")
      .description(
          "Checks indexed cylinders that contain unreferenced vertices.")
      .build([](BuildContext &ctx) {
        auto spec = cylinder(4);
        spec.mode = PrimitiveMode::Indexed;
        spec.unusedVertices = true;
        return surfaceWorld(ctx, withAxes(ctx, spec));
      })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CYLINDER", kMatte})
      .registerInto(catalog);

  // ---- curve ----------------------------------------------------------------
  registerShape(catalog,
      "curve",
      curve(16),
      "Checks basic curve geometry in soup and indexed modes.",
      "ANARI_KHR_GEOMETRY_CURVE",
      /*soupVariantOnly=*/false);

  makeTest("geometry", "curve_colors")
      .description("Checks primitive and vertex colors on curve geometry.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, curve(4)));
      })
      .permute("color", {"primitive.color", "vertex.color"})
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CURVE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "curve_frame_color_types")
      .description(
          "Checks curve rendering across supported color buffer types.")
      .build([](BuildContext &ctx) {
        return surfaceWorld(ctx, withAxes(ctx, curve(16)));
      })
      .permute("frame_color_type", kFrameColorTypes)
      .channels(kColorOnly)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CURVE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "curve_global_radii")
      .description("Checks the global radius parameter on curve geometry.")
      .build([](BuildContext &ctx) {
        auto spec = curve(16);
        spec.globalRadius = 0.05f;
        return surfaceWorld(ctx, withAxes(ctx, spec));
      })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CURVE", kMatte})
      .registerInto(catalog);

  makeTest("geometry", "curve_unused_vertices")
      .description("Checks indexed curves that contain unreferenced vertices.")
      .build([](BuildContext &ctx) {
        auto spec = curve(4);
        spec.mode = PrimitiveMode::Indexed;
        spec.unusedVertices = true;
        return surfaceWorld(ctx, withAxes(ctx, spec));
      })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_GEOMETRY_CURVE", kMatte})
      .registerInto(catalog);

  // ---- isosurface -----------------------------------------------------------
  makeTest("geometry", "isosurface")
      .description(
          "Checks isosurface extraction from a structured regular field.")
      .build([](BuildContext &ctx) { return isosurfaceWorld(ctx); })
      .channels(kColorDepth)
      .requireFeatures({"ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR",
          "ANARI_KHR_GEOMETRY_ISOSURFACE",
          kMatte})
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
