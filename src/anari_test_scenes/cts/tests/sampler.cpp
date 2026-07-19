// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "../BuildContext.h"
#include "../LightBuilder.h"
#include "../SamplerBuilder.h"
#include "../SurfaceBuilder.h"
#include "../TestBuilder.h"
#include "../WorldBuilder.h"
#include "Categories.h"
#include "generators/ColorPalette.h"
// std
#include <functional>
#include <string>
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float2;
using anari::math::float3;
using anari::math::float4;
using anari::math::mat4;
using anari::math::uint3;
using V = std::vector<Any>;

const char *kMatte = "ANARI_KHR_MATERIAL_MATTE";

// The transform/offset constants the JSON scenes reused (column-major: each
// argument is a matrix column; the last column is the translation).
const mat4 kInTransform = mat4(float4(-0.5f, -1.f, 0.f, 0.f),
    float4(1.f, -0.5f, 0.f, 0.f),
    float4(0.f, 0.f, -0.5f, 0.f),
    float4(1.f, 1.f, 1.f, 1.f));
const mat4 kOutTransform = mat4(float4(-1.f, -1.f, 0.f, 0.f),
    float4(1.f, -1.f, 0.f, 0.f),
    float4(0.f, 0.f, -1.f, 0.f),
    float4(1.f, 1.f, 1.f, 1.f));
const float4 kOffset = float4(0.7f, -0.4f, 0.6f, -0.2f);

using GeomFn = std::function<void(anari::Device, anari::Geometry)>;
using SamplerFn =
    std::function<void(BuildContext &, anari::Device, anari::Sampler)>;

// A quad (two triangles) facing the camera, with attribute0 = palette colors
// (used by the transform sampler). Uncommitted so the caller can add the
// per-test texcoord attribute.
anari::Geometry samplerQuad(anari::Device d)
{
  std::vector<float3> pos = {
      {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {1.f, 1.f, 0.f}};
  std::vector<uint3> idx = {{0u, 1u, 2u}, {2u, 1u, 3u}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setAndReleaseParameter(
      d, geom, "vertex.position", anari::newArray1D(d, pos.data(), pos.size()));
  anari::setAndReleaseParameter(
      d, geom, "primitive.index", anari::newArray1D(d, idx.data(), idx.size()));
  std::vector<float4> cols;
  for (size_t i = 0; i < pos.size(); ++i)
    cols.emplace_back(scenes::colors::getColorFromPalette(i), 1.f);
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.attribute0",
      anari::newArray1D(d, cols.data(), cols.size()));
  return geom;
}

anari::World samplerWorld(BuildContext &ctx,
    const char *samplerSubtype,
    const char *inAttribute,
    const GeomFn &setupGeom,
    const SamplerFn &setupSampler)
{
  auto d = ctx.device();
  auto geom = samplerQuad(d);
  if (setupGeom)
    setupGeom(d, geom);
  anari::commitParameters(d, geom);

  const std::string st = samplerSubtype;
  anari::Sampler sampler;
  if (st.rfind("image", 0) == 0) {
    sampler = newImageSampler(d, st, inAttribute, false);
  } else {
    sampler = anari::newObject<anari::Sampler>(d, st.c_str());
    if (inAttribute && *inAttribute)
      anari::setParameter(d, sampler, "inAttribute", inAttribute);
  }
  if (setupSampler)
    setupSampler(ctx, d, sampler);
  anari::commitParameters(d, sampler);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", sampler);
  anari::commitParameters(d, mat);

  auto surface = makeSurface(d, geom, mat);
  auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));
  WorldContents wc;
  wc.surfaces = {surface};
  wc.lights = {light};
  auto world = assembleWorld(d, wc);

  anari::release(d, geom);
  anari::release(d, sampler);
  anari::release(d, mat);
  anari::release(d, surface);
  anari::release(d, light);
  return world;
}

void set(
    BuildContext &ctx, anari::Device d, anari::Sampler s, const char *param)
{
  applyParameterValue(d, s, param, ctx.value(param).raw());
}

} // namespace

void registerSamplerTests(Catalog &catalog)
{
  const V filter = {none(), Any("nearest"), Any("linear")};
  const V wrap = {
      none(), Any("clampToEdge"), Any("repeat"), Any("mirrorRepeat")};
  const V xform = {none(), Any(kInTransform)};
  const V outXform = {none(), Any(kOutTransform)};
  const V offset = {none(), Any(kOffset)};

  // ---- image1D --------------------------------------------------------------
  makeTest("sampler", "image1D")
      .description(
          "Checks 1D image filtering, wrapping, transforms, and offsets.")
      .build([](BuildContext &ctx) {
        return samplerWorld(
            ctx,
            "image1D",
            "attribute1",
            [](anari::Device d, anari::Geometry g) {
              std::vector<float> uv = {0.f, 2.f, 0.f, 2.f};
              anari::setAndReleaseParameter(d,
                  g,
                  "vertex.attribute1",
                  anari::newArray1D(d, uv.data(), uv.size()));
            },
            [](BuildContext &ctx, anari::Device d, anari::Sampler s) {
              set(ctx, d, s, "filter");
              set(ctx, d, s, "wrapMode");
              set(ctx, d, s, "inTransform");
              set(ctx, d, s, "outTransform");
              set(ctx, d, s, "inOffset");
              set(ctx, d, s, "outOffset");
            });
      })
      .simplified()
      .permute("filter", filter)
      .permute("wrapMode", wrap)
      .permute("inTransform", xform)
      .permute("outTransform", outXform)
      .permute("inOffset", offset)
      .permute("outOffset", offset)
      .requireFeatures({"ANARI_KHR_SAMPLER_IMAGE1D", kMatte})
      .registerInto(catalog);

  // ---- image2D --------------------------------------------------------------
  makeTest("sampler", "image2D")
      .description(
          "Checks 2D image filtering, wrapping, transforms, and offsets.")
      .build([](BuildContext &ctx) {
        return samplerWorld(
            ctx,
            "image2D",
            "attribute1",
            [](anari::Device d, anari::Geometry g) {
              std::vector<float2> uv = {
                  {0.f, 0.f}, {2.f, 0.f}, {0.f, 2.f}, {2.f, 2.f}};
              anari::setAndReleaseParameter(d,
                  g,
                  "vertex.attribute1",
                  anari::newArray1D(d, uv.data(), uv.size()));
            },
            [](BuildContext &ctx, anari::Device d, anari::Sampler s) {
              set(ctx, d, s, "filter");
              set(ctx, d, s, "wrapMode1");
              set(ctx, d, s, "wrapMode2");
              set(ctx, d, s, "inTransform");
              set(ctx, d, s, "outTransform");
              set(ctx, d, s, "inOffset");
              set(ctx, d, s, "outOffset");
            });
      })
      .simplified()
      .permute("filter", filter)
      .permute("wrapMode1", wrap)
      .permute("wrapMode2", wrap)
      .permute("inTransform", xform)
      .permute("outTransform", outXform)
      .permute("inOffset", offset)
      .permute("outOffset", offset)
      .requireFeatures({"ANARI_KHR_SAMPLER_IMAGE2D", kMatte})
      .registerInto(catalog);

  // ---- image3D --------------------------------------------------------------
  makeTest("sampler", "image3D")
      .description(
          "Checks 3D image filtering, wrapping, transforms, and offsets.")
      .build([](BuildContext &ctx) {
        return samplerWorld(
            ctx,
            "image3D",
            "attribute1",
            [](anari::Device d, anari::Geometry g) {
              std::vector<float3> uv = {{0.f, 0.f, 0.f},
                  {2.f, 0.f, 0.f},
                  {0.f, 2.f, 0.f},
                  {2.f, 2.f, 2.f}};
              anari::setAndReleaseParameter(d,
                  g,
                  "vertex.attribute1",
                  anari::newArray1D(d, uv.data(), uv.size()));
            },
            [](BuildContext &ctx, anari::Device d, anari::Sampler s) {
              set(ctx, d, s, "filter");
              set(ctx, d, s, "wrapMode1");
              set(ctx, d, s, "wrapMode2");
              set(ctx, d, s, "wrapMode3");
              set(ctx, d, s, "inTransform");
              set(ctx, d, s, "outTransform");
              set(ctx, d, s, "inOffset");
              set(ctx, d, s, "outOffset");
            });
      })
      .simplified()
      .permute("filter", filter)
      .permute("wrapMode1", wrap)
      .permute("wrapMode2", wrap)
      .permute("wrapMode3", wrap)
      .permute("inTransform", xform)
      .permute("outTransform", outXform)
      .permute("inOffset", offset)
      .permute("outOffset", offset)
      .requireFeatures({"ANARI_KHR_SAMPLER_IMAGE3D", kMatte})
      .registerInto(catalog);

  // ---- primitive ------------------------------------------------------------
  // The array value is itself an axis; encode the per-Case element width as a
  // token and build the (2-element) array of that width in the sampler setup.
  makeTest("sampler", "primitive")
      .description("Checks primitive sampler offsets and array element widths.")
      .build([](BuildContext &ctx) {
        return samplerWorld(ctx,
            "primitive",
            "",
            nullptr,
            [](BuildContext &ctx, anari::Device d, anari::Sampler s) {
              applyParameterValue(d, s, "offset", ctx.value("offset").raw());
              const std::string dim = ctx.getString("arrayDim", "1");
              if (dim == "1") {
                std::vector<float> a = {0.5f, 1.0f};
                anari::setAndReleaseParameter(
                    d, s, "array", anari::newArray1D(d, a.data(), a.size()));
              } else if (dim == "2") {
                std::vector<float2> a = {{0.5f, 0.5f}, {1.f, 1.f}};
                anari::setAndReleaseParameter(
                    d, s, "array", anari::newArray1D(d, a.data(), a.size()));
              } else if (dim == "3") {
                std::vector<float3> a = {{0.5f, 0.5f, 0.5f}, {1.f, 1.f, 1.f}};
                anari::setAndReleaseParameter(
                    d, s, "array", anari::newArray1D(d, a.data(), a.size()));
              } else {
                std::vector<float4> a = {
                    {0.5f, 0.5f, 0.5f, 0.5f}, {1.f, 1.f, 1.f, 1.f}};
                anari::setAndReleaseParameter(
                    d, s, "array", anari::newArray1D(d, a.data(), a.size()));
              }
            });
      })
      .permute("offset", V{none(), Any(1)})
      .permute("arrayDim", {"1", "2", "3", "4"})
      .requireFeatures({"ANARI_KHR_SAMPLER_PRIMITIVE", kMatte})
      .registerInto(catalog);

  // ---- transform ------------------------------------------------------------
  makeTest("sampler", "transform")
      .description("Checks transform sampler matrices and output offsets.")
      .build([](BuildContext &ctx) {
        return samplerWorld(ctx,
            "transform",
            "attribute0",
            nullptr,
            [](BuildContext &ctx, anari::Device d, anari::Sampler s) {
              set(ctx, d, s, "transform");
              set(ctx, d, s, "outOffset");
            });
      })
      .permute("transform", V{none(), Any(kOutTransform)})
      .permute("outOffset", offset)
      .requireFeatures({"ANARI_KHR_SAMPLER_TRANSFORM", kMatte})
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
