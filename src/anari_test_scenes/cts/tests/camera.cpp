// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "../BuildContext.h"
#include "../GeometryBuilder.h"
#include "../LightBuilder.h"
#include "../SurfaceBuilder.h"
#include "../TestBuilder.h"
#include "../WorldBuilder.h"
#include "Categories.h"
// std
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float3;
using V = std::vector<Any>;

const char *kTriangle = "ANARI_KHR_GEOMETRY_TRIANGLE";

// A fixed lit triangle surface, the subject every camera test views.
anari::World cameraSubjectWorld(BuildContext &ctx)
{
  auto d = ctx.device();
  TriangleSpec spec;
  spec.primitiveCount = 16;
  auto geom = buildTriangleGeometry(d, spec);
  auto mat = makeMatteMaterial(d, float3(0.7f, 0.5f, 0.3f));
  auto surface = makeSurface(d, geom, mat);
  auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));
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

// An imageRegion (box2) axis value: a 2x2 box stored with its ANARI type so it
// is set as FLOAT32_BOX2, not a plain vec4.
Any box2(float lx, float ly, float hx, float hy)
{
  const float v[4] = {lx, ly, hx, hy};
  return Any(ANARI_FLOAT32_BOX2, v);
}

void cp(BuildContext &ctx, anari::Device d, anari::Camera c, const char *param)
{
  applyParameterValue(d, c, param, ctx.value(param).raw());
}

} // namespace

void registerCameraTests(Catalog &catalog)
{
  // ---- perspective: general (direction / up / imageRegion) ------------------
  makeTest("camera", "camera_general")
      .build(cameraSubjectWorld)
      .camera([](BuildContext &ctx, const scenes::Bounds &) -> anari::Camera {
        auto d = ctx.device();
        auto c = anari::newObject<anari::Camera>(d, "perspective");
        anari::setParameter(d, c, "position", float3(0.5f, 0.5f, -1.f));
        anari::setParameter(d, c, "near", 0.01f);
        anari::setParameter(d, c, "far", 100.f);
        cp(ctx, d, c, "direction");
        cp(ctx, d, c, "up");
        cp(ctx, d, c, "imageRegion");
        anari::commitParameters(d, c);
        return c;
      })
      .simplified()
      .permute("direction", V{none(), Any(float3(0.20601f, 0.f, 0.97855f))})
      .permute("up", V{none(), Any(float3(0.f, -0.97855f, 0.20601f))})
      .permute("imageRegion", V{none(), box2(0.25f, 0.3f, 0.75f, 0.8f)})
      .requireFeatures({"ANARI_KHR_CAMERA_PERSPECTIVE", kTriangle})
      .registerInto(catalog);

  // ---- perspective: intrinsics ----------------------------------------------
  makeTest("camera", "perspective")
      .build(cameraSubjectWorld)
      .camera([](BuildContext &ctx, const scenes::Bounds &) -> anari::Camera {
        auto d = ctx.device();
        auto c = anari::newObject<anari::Camera>(d, "perspective");
        anari::setParameter(d, c, "position", float3(0.5f, 0.5f, -1.f));
        cp(ctx, d, c, "fovy");
        cp(ctx, d, c, "aspect");
        cp(ctx, d, c, "near");
        cp(ctx, d, c, "far");
        anari::commitParameters(d, c);
        return c;
      })
      .simplified()
      .permute("fovy", V{none(), Any(0.6f)})
      .permute("aspect", V{none(), Any(1.77f)})
      .permute("near", V{Any(0.01f), Any(1.0f)})
      .permute("far", V{Any(100.0f), Any(1.0f)})
      .requireFeatures({"ANARI_KHR_CAMERA_PERSPECTIVE", kTriangle})
      .registerInto(catalog);

  // ---- orthographic ---------------------------------------------------------
  makeTest("camera", "orthographic")
      .build(cameraSubjectWorld)
      .camera([](BuildContext &ctx, const scenes::Bounds &) -> anari::Camera {
        auto d = ctx.device();
        auto c = anari::newObject<anari::Camera>(d, "orthographic");
        anari::setParameter(d, c, "position", float3(0.5f, 0.5f, -1.f));
        cp(ctx, d, c, "aspect");
        cp(ctx, d, c, "height");
        cp(ctx, d, c, "near");
        cp(ctx, d, c, "far");
        anari::commitParameters(d, c);
        return c;
      })
      .simplified()
      .permute("aspect", V{none(), Any(1.77f)})
      .permute("height", V{none(), Any(0.5f)})
      .permute("near", V{Any(0.01f), Any(1.0f)})
      .permute("far", V{Any(100.0f), Any(1.0f)})
      .requireFeatures({"ANARI_KHR_CAMERA_ORTHOGRAPHIC", kTriangle})
      .registerInto(catalog);

  // ---- omnidirectional ------------------------------------------------------
  makeTest("camera", "omnidirectional")
      .build(cameraSubjectWorld)
      .camera([](BuildContext &ctx, const scenes::Bounds &) -> anari::Camera {
        auto d = ctx.device();
        auto c = anari::newObject<anari::Camera>(d, "omnidirectional");
        anari::setParameter(d, c, "position", float3(0.5f, 0.5f, -1.f));
        cp(ctx, d, c, "layout");
        anari::commitParameters(d, c);
        return c;
      })
      .permute("layout", V{none(), Any("equirectangular")})
      .requireFeatures({"ANARI_KHR_CAMERA_OMNIDIRECTIONAL", kTriangle})
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
