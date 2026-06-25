// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Light conformance tests, migrated from cts/test_scenes/light/**. Each lights a
// matte triangle surface with one light whose parameters are permuted, under a
// renderer with a black background and no ambient term so only the light
// contributes. (helide exposes no light extension, so these register and skip
// there; they run on devices that support the light subtypes.)

#include "Categories.h"
#include "../BuildContext.h"
#include "../TestBuilder.h"
#include "../WorldBuilder.h"
// std
#include <functional>
#include <string>
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float3;
using anari::math::float4;
using V = std::vector<Any>;

using LightFn = std::function<void(BuildContext &, anari::Device, anari::Light)>;

anari::World lightWorld(BuildContext &ctx, const char *subtype, const LightFn &apply)
{
  auto d = ctx.device();
  GeometryOptions o;
  o.subtype = "triangle";
  o.shape = "triangle";
  o.primitiveCount = 16;
  o.seed = 12345;
  auto geom = buildGeometry(d, o);
  auto mat = makeMatteMaterial(d, float3(0.7f, 0.7f, 0.7f));
  auto surface = makeSurface(d, geom, mat);

  auto light = newLight(d, subtype);
  apply(ctx, d, light);
  anari::commitParameters(d, light);

  WorldContents wc;
  wc.surfaces = {surface};
  wc.lights = {light};
  auto world = assembleWorld(d, wc);

  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  anari::release(d, light);
  return world;
}

// A renderer with a black background and no ambient light, so the test light is
// the only illumination.
anari::Renderer darkRenderer(BuildContext &ctx)
{
  auto d = ctx.device();
  auto r = newRenderer(d, "default");
  anari::setParameter(d, r, "background", float4(0.f, 0.f, 0.f, 1.f));
  anari::setParameter(d, r, "ambientRadiance", 0.f);
  anari::setParameter(d, r, "ambientColor", float3(0.f, 0.f, 0.f));
  anari::commitParameters(d, r);
  return r;
}

void lp(BuildContext &ctx, anari::Device d, anari::Light l, const char *param)
{
  setBoundParameter(d, l, param, ctx.value(param));
}

// Set an intensityDistribution array from a per-Case token ("none"/"1d"/"2d").
void setIntensityDistribution(BuildContext &ctx, anari::Device d, anari::Light l)
{
  const std::string id = ctx.getString("intensityDistribution", "none");
  if (id == "1d") {
    std::vector<float> a = {0.2f, 0.8f, 0.4f, 0.6f};
    anari::setAndReleaseParameter(
        d, l, "intensityDistribution", anari::newArray1D(d, a.data(), a.size()));
  } else if (id == "2d") {
    std::vector<float> a = {
        0.2f, 0.8f, 0.4f, 0.3f, 0.7f, 0.5f, 0.1f, 0.9f, 0.6f};
    anari::setAndReleaseParameter(d,
        l,
        "intensityDistribution",
        anari::newArray2D(d, a.data(), 3, 3));
  }
}

} // namespace

void registerLightTests(Catalog &catalog)
{
  // ---- directional ----------------------------------------------------------
  makeTest("light", "directional")
      .build([](BuildContext &ctx) {
        return lightWorld(ctx, "directional",
            [](BuildContext &ctx, anari::Device d, anari::Light l) {
              anari::setParameter(d, l, "color", float3(0.7f, 0.62f, 0.78f));
              lp(ctx, d, l, "direction");
              lp(ctx, d, l, "irradiance");
            });
      })
      .renderer(darkRenderer)
      .simplified()
      .permute("direction",
          V{Any(float3(0.f, -0.242536f, -0.970143f)),
              Any(float3(0.f, 0.f, -0.970143f))})
      .permute("irradiance", V{none(), Any(0.5f)})
      .requireFeature("ANARI_KHR_LIGHT_DIRECTIONAL")
      .registerInto(catalog);

  // ---- point ----------------------------------------------------------------
  makeTest("light", "point")
      .build([](BuildContext &ctx) {
        return lightWorld(ctx, "point",
            [](BuildContext &ctx, anari::Device d, anari::Light l) {
              anari::setParameter(d, l, "color", float3(0.3f, 0.59f, 0.9f));
              lp(ctx, d, l, "position");
              lp(ctx, d, l, "intensity");
              lp(ctx, d, l, "power");
            });
      })
      .renderer(darkRenderer)
      .simplified()
      .permute("position",
          V{Any(float3(0.5f, 0.5f, 10.f)), Any(float3(0.5f, 0.5f, 1.f))})
      .permute("intensity", V{none(), Any(0.6f)})
      .permute("power", V{none(), Any(0.4f)})
      .requireFeature("ANARI_KHR_LIGHT_POINT")
      .registerInto(catalog);

  // ---- spot -----------------------------------------------------------------
  makeTest("light", "spot")
      .build([](BuildContext &ctx) {
        return lightWorld(ctx, "spot",
            [](BuildContext &ctx, anari::Device d, anari::Light l) {
              anari::setParameter(d, l, "color", float3(0.74f, 0.73f, 0.85f));
              lp(ctx, d, l, "position");
              lp(ctx, d, l, "direction");
              lp(ctx, d, l, "openingAngle");
              lp(ctx, d, l, "falloffAngle");
              lp(ctx, d, l, "intensity");
              lp(ctx, d, l, "power");
            });
      })
      .renderer(darkRenderer)
      .simplified()
      .permute("position",
          V{Any(float3(0.5f, 0.5f, 0.5f)), Any(float3(0.3f, 0.3f, 0.3f))})
      .permute("direction",
          V{Any(float3(0.f, -0.242536f, -0.970143f)),
              Any(float3(0.f, 0.f, -0.970143f))})
      .permute("openingAngle", V{Any(0.5f), none()})
      .permute("falloffAngle", V{none(), Any(1.1f)})
      .permute("intensity", V{none(), Any(0.6f)})
      .permute("power", V{none(), Any(0.4f)})
      .requireFeature("ANARI_KHR_LIGHT_SPOT")
      .registerInto(catalog);

  // ---- quad -----------------------------------------------------------------
  makeTest("light", "quad")
      .build([](BuildContext &ctx) {
        return lightWorld(ctx, "quad",
            [](BuildContext &ctx, anari::Device d, anari::Light l) {
              anari::setParameter(d, l, "color", float3(0.7f, 0.87f, 0.41f));
              lp(ctx, d, l, "position");
              lp(ctx, d, l, "edge1");
              lp(ctx, d, l, "edge2");
              lp(ctx, d, l, "intensity");
              lp(ctx, d, l, "power");
              lp(ctx, d, l, "radiance");
              lp(ctx, d, l, "side");
              setIntensityDistribution(ctx, d, l);
            });
      })
      .renderer(darkRenderer)
      .simplified()
      .permute("position", V{Any(float3(0.f, 0.f, 0.1f)), none()})
      .permute("edge1", V{Any(float3(0.f, 0.1f, -1.f)), none()})
      .permute("edge2", V{Any(float3(-0.1f, 1.f, 0.f)), none()})
      .permute("intensity", V{none(), Any(0.6f)})
      .permute("power", V{none(), Any(0.4f)})
      .permute("radiance", V{none(), Any(0.2f)})
      .permute("side", V{Any("both"), Any("front"), Any("back"), none()})
      .permute("intensityDistribution", {"none", "1d", "2d"})
      .requireFeature("ANARI_KHR_LIGHT_QUAD")
      .registerInto(catalog);

  // ---- ring -----------------------------------------------------------------
  makeTest("light", "ring")
      .build([](BuildContext &ctx) {
        return lightWorld(ctx, "ring",
            [](BuildContext &ctx, anari::Device d, anari::Light l) {
              anari::setParameter(d, l, "color", float3(0.98f, 0.5f, 0.44f));
              lp(ctx, d, l, "position");
              lp(ctx, d, l, "direction");
              lp(ctx, d, l, "radius");
              lp(ctx, d, l, "innerRadius");
              lp(ctx, d, l, "openingAngle");
              lp(ctx, d, l, "falloffAngle");
              lp(ctx, d, l, "intensity");
              lp(ctx, d, l, "power");
              lp(ctx, d, l, "radiance");
              lp(ctx, d, l, "c0");
              setIntensityDistribution(ctx, d, l);
            });
      })
      .renderer(darkRenderer)
      .simplified()
      .permute("position", V{Any(float3(0.2f, 0.2f, 0.1f)), none()})
      .permute("direction", V{Any(float3(0.f, -0.20601f, -0.97855f)), none()})
      .permute("radius", V{Any(0.3f), none()})
      .permute("innerRadius", V{Any(0.15f), none()})
      .permute("openingAngle", V{none(), Any(0.5f)})
      .permute("falloffAngle", V{none(), Any(1.1f)})
      .permute("intensity", V{none(), Any(0.6f)})
      .permute("power", V{none(), Any(0.4f)})
      .permute("radiance", V{none(), Any(0.2f)})
      .permute("c0", V{none(), Any(float3(0.7071f, 0.7071f, 0.f))})
      .permute("intensityDistribution", {"none", "1d", "2d"})
      .requireFeature("ANARI_KHR_LIGHT_RING")
      .registerInto(catalog);

  // ---- hdri -----------------------------------------------------------------
  makeTest("light", "hdri")
      .build([](BuildContext &ctx) {
        return lightWorld(ctx, "hdri",
            [](BuildContext &ctx, anari::Device d, anari::Light l) {
              lp(ctx, d, l, "up");
              lp(ctx, d, l, "direction");
              lp(ctx, d, l, "scale");
            });
      })
      .renderer(darkRenderer)
      .simplified()
      .permute("up", V{none(), Any(float3(0.f, 1.f, 0.f))})
      .permute("direction", V{none(), Any(float3(0.f, -1.f, 0.f))})
      .permute("scale", V{none(), Any(0.4f)})
      .requireFeature("ANARI_KHR_LIGHT_HDRI")
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
