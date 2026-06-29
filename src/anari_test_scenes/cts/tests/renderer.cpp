// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "../BuildContext.h"
#include "../GeometryBuilder.h"
#include "../SurfaceBuilder.h"
#include "../TestBuilder.h"
#include "../ViewBuilder.h"
#include "../WorldBuilder.h"
#include "Categories.h"
#include "generators/TextureGenerator.h"
// std
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float3;
using anari::math::float4;
using V = std::vector<Any>;

// A matte triangle surface lit only by the renderer's ambient term.
anari::World ambientSubject(BuildContext &ctx)
{
  auto d = ctx.device();
  TriangleSpec spec;
  spec.primitiveCount = 16;
  auto geom = buildTriangleGeometry(d, spec);
  auto mat = makeMatteMaterial(d, float3(0.7f, 0.7f, 0.7f));
  auto surface = makeSurface(d, geom, mat);
  WorldContents wc;
  wc.surfaces = {surface};
  auto world = assembleWorld(d, wc);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  return world;
}

// An empty world: the background-only tests render no geometry.
anari::World emptyWorld(BuildContext &ctx)
{
  return assembleWorld(ctx.device(), WorldContents{});
}

} // namespace

void registerRendererTests(Catalog &catalog)
{
  makeTest("renderer", "renderer_ambient")
      .build(ambientSubject)
      .renderer([](BuildContext &ctx) {
        auto d = ctx.device();
        auto r = newRenderer(d, "default");
        applyParameterValue(
            d, r, "ambientColor", ctx.value("ambientColor").raw());
        applyParameterValue(
            d, r, "ambientRadiance", ctx.value("ambientRadiance").raw());
        anari::commitParameters(d, r);
        return r;
      })
      .simplified()
      .permute("ambientColor",
          V{Any(float3(0.f, 0.f, 1.f)),
              Any(float3(0.5f, 0.5f, 0.5f)),
              Any(float3(0.f, 0.f, 0.f)),
              Any(float3(1.f, 1.f, 1.f))})
      .permute("ambientRadiance", V{Any(0.5f), Any(4.0f), Any(0.0f)})
      .requireFeature("ANARI_KHR_RENDERER_AMBIENT_LIGHT")
      .registerInto(catalog);

  makeTest("renderer", "renderer_background_color")
      .build(emptyWorld)
      .renderer([](BuildContext &ctx) {
        auto d = ctx.device();
        auto r = newRenderer(d, "default");
        applyParameterValue(d, r, "background", ctx.value("background").raw());
        anari::commitParameters(d, r);
        return r;
      })
      .permute("background",
          V{Any(float4(0.f, 0.f, 0.f, 1.f)),
              Any(float4(0.5f, 0.5f, 0.5f, 1.f)),
              Any(float4(0.f, 0.f, 1.f, 1.f)),
              Any(float4(0.f, 0.f, 1.f, 0.5f))})
      .requireFeature("ANARI_KHR_RENDERER_BACKGROUND_COLOR")
      .registerInto(catalog);

  makeTest("renderer", "renderer_background_image")
      .build(emptyWorld)
      .renderer([](BuildContext &ctx) {
        auto d = ctx.device();
        auto r = newRenderer(d, "default");
        const size_t res = 32;
        auto img = scenes::TextureGenerator::generateCheckerBoard(res);
        anari::setAndReleaseParameter(
            d, r, "background", anari::newArray2D(d, img.data(), res, res));
        anari::commitParameters(d, r);
        return r;
      })
      .requireFeature("ANARI_KHR_RENDERER_BACKGROUND_IMAGE")
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
