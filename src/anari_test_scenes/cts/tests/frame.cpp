// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "../AnariObject.h"
#include "../BuildContext.h"
#include "../FrameReadback.h"
#include "../GeometryBuilder.h"
#include "../GeometryLayout.h"
#include "../InstanceBuilder.h"
#include "../LightBuilder.h"
#include "../SurfaceBuilder.h"
#include "../TestBuilder.h"
#include "../TestDef.h"
#include "../VolumeBuilder.h"
#include "../WorldBuilder.h"
#include "Categories.h"
// std
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float3;
using V = std::vector<Any>;

const char *kTriangle = "ANARI_KHR_GEOMETRY_TRIANGLE";

// A single lit triangle surface (the subject for the simple channel tests).
anari::World triangleSurfaceWorld(BuildContext &ctx, float3 color, int count)
{
  auto d = ctx.device();
  TriangleSpec spec;
  spec.primitiveCount = count;
  auto geom = buildTriangleGeometry(d, spec);
  auto mat = makeMatteMaterial(d, color);
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

// A surface over a single canonical triangle translated by `offset`, optionally
// tagged with an id. The id tests place two of these at different offsets so
// the two ids occupy distinct, non-overlapping regions of the id channel.
anari::Surface idSurface(
    anari::Device d, float3 offset, uint32_t id, bool setId)
{
  auto verts = layoutTriangleSoup(1);
  for (auto &v : verts)
    v = v + offset;
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.position",
      anari::newArray1D(d, verts.data(), verts.size()));
  anari::commitParameters(d, geom);
  auto mat = makeMatteMaterial(d, float3(0.7f, 0.7f, 0.7f));
  auto s = anari::newObject<anari::Surface>(d);
  anari::setParameter(d, s, "geometry", geom);
  anari::setParameter(d, s, "material", mat);
  if (setId)
    anari::setParameter(d, s, "id", id);
  anari::commitParameters(d, s);
  anari::release(d, geom);
  anari::release(d, mat);
  return s;
}

// An instance wrapping a group with one surface, translated by `offset` so two
// instances occupy distinct regions, optionally tagged with an id.
anari::Instance idInstance(
    anari::Device d, float3 offset, uint32_t id, bool setId)
{
  auto s = idSurface(d, float3(0.f), 0, false);
  auto inst = makeInstance(d, {s}, anari::math::translation_matrix(offset));
  anari::release(d, s);
  if (setId) {
    anari::setParameter(d, inst, "id", id);
    anari::commitParameters(d, inst);
  }
  return inst;
}

// A frame carrying the runner's world/camera/renderer, ready to render.
UniqueAnariObject<anari::Frame> behaviorFrame(anari::Device d,
    anari::World world,
    anari::Camera camera,
    anari::Renderer renderer,
    uint32_t w,
    uint32_t h)
{
  UniqueAnariObject<anari::Frame> frame(d, anari::newObject<anari::Frame>(d));
  const auto f = frame.get();
  anari::setParameter(d, f, "size", anari::math::vec<uint32_t, 2>(w, h));
  anari::setParameter(d, f, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  anari::setParameter(d, f, "renderer", renderer);
  anari::setParameter(d, f, "camera", camera);
  anari::setParameter(d, f, "world", world);
  return frame;
}

// Behavioral check: the device must invoke the frame-completion callback
// exactly when a frame finishes. Passes if the callback fired during
// render/wait.
BehaviorResult checkFrameCompletionCallback(anari::Device d,
    anari::World world,
    anari::Camera camera,
    anari::Renderer renderer,
    uint32_t w,
    uint32_t h)
{
  auto frame = behaviorFrame(d, world, camera, renderer, w, h);
  const auto f = frame.get();

  int fired = 0;
  anari::setParameter(d,
      f,
      "frameCompletionCallback",
      (anari::FrameCompletionCallback)(
          +[](const void *userPtr, ANARIDevice, ANARIFrame) {
            if (userPtr)
              ++*static_cast<int *>(const_cast<void *>(userPtr));
          }));
  anari::setParameter(
      d, f, "frameCompletionCallbackUserData", static_cast<void *>(&fired));
  anari::commitParameters(d, f);

  anari::render(d, f);
  anari::wait(d, f);
  // A conformant device fires the callback during the wait above (this assumes
  // synchronous completion).
  const bool ok = fired > 0;

  return {ok,
      ok ? "frame completion callback fired"
         : "frame completion callback did not fire"};
}

// Behavioral check: a progressive renderer accumulates across successive
// renders of the same frame, refining the image; a non-progressive one
// reproduces an identical image. Render once, accumulate several more renders,
// then compare against the first. The 10-render accumulation and the >10-pixel
// bar mirror the original behavioral test (which compared frame 1 against the
// 10x-accumulated frame), not just two consecutive frames.
BehaviorResult checkProgressiveRendering(anari::Device d,
    anari::World world,
    anari::Camera camera,
    anari::Renderer renderer,
    uint32_t w,
    uint32_t h)
{
  auto frame = behaviorFrame(d, world, camera, renderer, w, h);
  const auto f = frame.get();
  anari::setParameter(d, f, "accumulation", true);
  anari::commitParameters(d, f);

  auto snapshot = [&]() {
    return readFrameChannel(d, f, Channel::Color, w, h);
  };

  anari::render(d, f);
  anari::wait(d, f);
  const auto first = snapshot();

  constexpr int kAccumulationFrames = 10;
  for (int i = 0; i < kAccumulationFrames; ++i) {
    anari::render(d, f);
    anari::wait(d, f);
  }
  const auto accumulated = snapshot();

  if (!first)
    return {false, "initial color readback failed: " + first.detail};
  if (!accumulated)
    return {false, "accumulated color readback failed: " + accumulated.detail};

  size_t changed = 0;
  for (size_t i = 0; i < first.image.rgba.size(); i += 4) {
    if (!std::equal(first.image.rgba.begin() + i,
            first.image.rgba.begin() + i + 4,
            accumulated.image.rgba.begin() + i))
      ++changed;
  }

  const bool ok = changed > 10;
  return {ok, std::to_string(changed) + " pixels changed after accumulation"};
}

} // namespace

void registerFrameTests(Catalog &catalog)
{
  const V colorTypes = {
      Any("UFIXED8_VEC4"), Any("UFIXED8_RGBA_SRGB"), Any("FLOAT32_VEC4")};

  // ---- color channel output types -------------------------------------------
  makeTest("frame", "frame_color_channel")
      .description("Checks color channel output across supported buffer types.")
      .build([](BuildContext &ctx) {
        return triangleSurfaceWorld(ctx, float3(0.55f, 0.82f, 0.78f), 16);
      })
      .permute("frame_color_type", colorTypes)
      .channels({Channel::Color})
      .requireFeature(kTriangle)
      .registerInto(catalog);

  // ---- depth channel --------------------------------------------------------
  makeTest("frame", "frame_depth_channel")
      .description("Checks depth channel values for rendered geometry.")
      .build([](BuildContext &ctx) {
        return triangleSurfaceWorld(ctx, float3(0.7f, 0.5f, 0.3f), 16);
      })
      .channels({Channel::Depth})
      .requireFeature(kTriangle)
      .registerInto(catalog);

  // ---- albedo channel output types (skips where unsupported) ----------------
  // The runner renders albedo at the axis's buffer format (UFIXED8_VEC3 /
  // UFIXED8_RGB_SRGB / FLOAT32_VEC3) and reads it back accordingly (D2). helide
  // has no albedo channel, so these still skip there.
  makeTest("frame", "frame_albedo_channel")
      .description(
          "Checks albedo channel output across supported buffer types.")
      .build([](BuildContext &ctx) {
        return triangleSurfaceWorld(ctx, float3(0.55f, 0.82f, 0.78f), 16);
      })
      .permute("frame_albedo_type",
          {"UFIXED8_VEC3", "UFIXED8_RGB_SRGB", "FLOAT32_VEC3"})
      .channels({Channel::Albedo})
      .requireFeature("ANARI_KHR_FRAME_CHANNEL_ALBEDO")
      .registerInto(catalog);

  // ---- normal channel output types ------------------------------------------
  makeTest("frame", "frame_normal_channel")
      .description(
          "Checks normal channel output across supported buffer types.")
      .build([](BuildContext &ctx) {
        return triangleSurfaceWorld(ctx, float3(0.7f, 0.5f, 0.3f), 1);
      })
      .permute("frame_normal_type", {"FIXED16_VEC3", "FLOAT32_VEC3"})
      .channels({Channel::Normal})
      .requireFeature("ANARI_KHR_FRAME_CHANNEL_NORMAL")
      .registerInto(catalog);

  // ---- primitive id channel -------------------------------------------------
  makeTest("frame", "frame_primitiveID_channel")
      .description("Checks primitive IDs returned by the frame channel.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        TriangleSpec spec;
        spec.primitiveCount = 8;
        auto geom = buildTriangleGeometry(d, spec);
        anari::commitParameters(d, geom);
        auto mat = makeMatteMaterial(d, float3(0.7f, 0.7f, 0.7f));
        auto surface = makeSurface(d, geom, mat);
        WorldContents wc;
        wc.surfaces = {surface};
        auto world = assembleWorld(d, wc);
        anari::release(d, geom);
        anari::release(d, mat);
        anari::release(d, surface);
        return world;
      })
      .channels({Channel::PrimitiveId})
      .requireFeature("ANARI_KHR_FRAME_CHANNEL_PRIMITIVE_ID")
      .registerInto(catalog);

  // ---- object id channel (per-surface ids) ----------------------------------
  makeTest("frame", "frame_objectID_channel_surface")
      .description("Checks surface object IDs returned by the frame channel.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        auto s0 = idSurface(d, float3(-0.3f, 0.f, 0.f), 1, true);
        auto s1 = idSurface(d, float3(0.3f, 0.f, 0.f), 0, true);
        WorldContents wc;
        wc.surfaces = {s0, s1};
        auto world = assembleWorld(d, wc);
        anari::release(d, s0);
        anari::release(d, s1);
        return world;
      })
      .channels({Channel::ObjectId})
      .requireFeature("ANARI_KHR_FRAME_CHANNEL_OBJECT_ID")
      .registerInto(catalog);

  // ---- object id channel (fallback to group/instance index) -----------------
  makeTest("frame", "frame_objectID_channel_group")
      .description(
          "Checks fallback object IDs for instanced groups and surfaces.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        auto i0 = idInstance(d, float3(-0.3f, 0.f, 0.f), 0, false);
        auto i1 = idInstance(d, float3(0.3f, 0.f, 0.f), 0, false);
        WorldContents wc;
        wc.instances = {i0, i1};
        auto world = assembleWorld(d, wc);
        anari::release(d, i0);
        anari::release(d, i1);
        return world;
      })
      .channels({Channel::ObjectId})
      .requireFeature("ANARI_KHR_FRAME_CHANNEL_OBJECT_ID")
      .registerInto(catalog);

  // ---- object id channel (per-volume ids) -----------------------------------
  makeTest("frame", "frame_objectID_channel_volume")
      .description("Checks volume object IDs returned by the frame channel.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        // Two volumes side by side (origin shifted along X) so the two ids
        // occupy distinct, non-overlapping regions of the id channel.
        auto makeVol = [&](float originX, uint32_t id) {
          auto field = newStructuredRegularField(d, {3, 3, 3});
          anari::setParameter(d, field, "origin", float3(originX, -1.f, -1.f));
          anari::commitParameters(d, field);
          auto vol = makeVolume(d, field);
          anari::setParameter(d, vol, "id", id);
          anari::commitParameters(d, vol);
          anari::release(d, field);
          return vol;
        };
        auto v0 = makeVol(-2.5f, 1);
        auto v1 = makeVol(0.5f, 0);
        WorldContents wc;
        wc.volumes = {v0, v1};
        auto world = assembleWorld(d, wc);
        anari::release(d, v0);
        anari::release(d, v1);
        return world;
      })
      .channels({Channel::ObjectId})
      .requireFeatures({"ANARI_KHR_FRAME_CHANNEL_OBJECT_ID",
          "ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR",
          "ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D"})
      .registerInto(catalog);

  // ---- instance id channel --------------------------------------------------
  makeTest("frame", "frame_instanceID_channel")
      .description("Checks instance IDs returned by the frame channel.")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        auto i0 = idInstance(d, float3(-0.3f, 0.f, 0.f), 1, true);
        auto i1 = idInstance(d, float3(0.3f, 0.f, 0.f), 0, true);
        WorldContents wc;
        wc.instances = {i0, i1};
        auto world = assembleWorld(d, wc);
        anari::release(d, i0);
        anari::release(d, i1);
        return world;
      })
      .channels({Channel::InstanceId})
      .requireFeature("ANARI_KHR_FRAME_CHANNEL_INSTANCE_ID")
      .registerInto(catalog);

  // ---- behavioral checks (verified by the runner's behavior hook) -----------
  makeTest("frame", "frame_completion_callback")
      .description(
          "Checks that rendering invokes the frame completion callback.")
      .build([](BuildContext &ctx) {
        return triangleSurfaceWorld(ctx, float3(0.7f, 0.5f, 0.3f), 1);
      })
      .behavior(checkFrameCompletionCallback)
      .requireFeature("ANARI_KHR_FRAME_COMPLETION_CALLBACK")
      .registerInto(catalog);

  makeTest("frame", "progressive_rendering")
      .description(
          "Checks that progressive frame accumulation converges over time.")
      .build([](BuildContext &ctx) {
        return triangleSurfaceWorld(ctx, float3(0.7f, 0.5f, 0.3f), 1);
      })
      .behavior(checkProgressiveRendering)
      .requireFeature("ANARI_KHR_FRAME_ACCUMULATION")
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
