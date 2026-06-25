// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Frame conformance tests, migrated from cts/test_scenes/frame/**. Cover the
// frame output channels: color (output type permutations), depth, albedo,
// normal, and the primitive/object/instance id channels (built from multi-
// surface / multi-instance / multi-volume worlds with explicit ids).
//
// Two of the originals are behavioral checks the old suite implemented with a
// synthetic pass/fail image (frame_completion_callback, progressive_rendering).
// The render-and-compare runner can't express that assertion, so they are
// registered as ordinary color renders gated on their feature, keeping the
// corpus complete; the behavioral guarantee is not verified here.

#include "Categories.h"
#include "../BuildContext.h"
#include "../TestBuilder.h"
#include "../WorldBuilder.h"
// std
#include <cstdint>
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
  GeometryOptions o;
  o.subtype = "triangle";
  o.shape = "triangle";
  o.primitiveCount = count;
  o.seed = 12345;
  auto geom = buildGeometry(d, o);
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

// A surface over a single-triangle geometry, optionally tagged with an id.
anari::Surface idSurface(anari::Device d, int seed, uint32_t id, bool setId)
{
  GeometryOptions o;
  o.subtype = "triangle";
  o.shape = "triangle";
  o.primitiveCount = 1;
  o.seed = seed;
  auto geom = buildGeometry(d, o);
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

// An instance wrapping a group with one surface, optionally tagged with an id.
anari::Instance idInstance(anari::Device d, int seed, uint32_t id, bool setId)
{
  auto s = idSurface(d, seed, 0, false);
  auto group = anari::newObject<anari::Group>(d);
  anari::setAndReleaseParameter(d, group, "surface", anari::newArray1D(d, &s, 1));
  anari::commitParameters(d, group);
  anari::release(d, s);
  auto inst = anari::newObject<anari::Instance>(d, "transform");
  if (setId)
    anari::setParameter(d, inst, "id", id);
  anari::setAndReleaseParameter(d, inst, "group", group);
  anari::commitParameters(d, inst);
  return inst;
}

} // namespace

void registerFrameTests(Catalog &catalog)
{
  const V colorTypes = {
      Any("UFIXED8_VEC4"), Any("UFIXED8_RGBA_SRGB"), Any("FLOAT32_VEC4")};

  // ---- color channel output types -------------------------------------------
  makeTest("frame", "frame_color_channel")
      .build([](BuildContext &ctx) {
        return triangleSurfaceWorld(ctx, float3(0.55f, 0.82f, 0.78f), 16);
      })
      .permute("frame_color_type", colorTypes)
      .channels({Channel::Color})
      .requireFeature(kTriangle)
      .registerInto(catalog);

  // ---- depth channel --------------------------------------------------------
  makeTest("frame", "frame_depth_channel")
      .build([](BuildContext &ctx) {
        return triangleSurfaceWorld(ctx, float3(0.7f, 0.5f, 0.3f), 16);
      })
      .channels({Channel::Depth})
      .requireFeature(kTriangle)
      .registerInto(catalog);

  // ---- albedo channel output types (skips where unsupported) ----------------
  // The runner renders albedo as FLOAT32_VEC3; the output-type axis distinguishes
  // ground truth but is not itself honored as a buffer format (helide has no
  // albedo channel, so these skip there).
  makeTest("frame", "frame_albedo_channel")
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
      .build([](BuildContext &ctx) {
        return triangleSurfaceWorld(ctx, float3(0.7f, 0.5f, 0.3f), 1);
      })
      .permute("frame_normal_type", {"FIXED16_VEC3", "FLOAT32_VEC3"})
      .channels({Channel::Normal})
      .requireFeature("ANARI_KHR_FRAME_CHANNEL_NORMAL")
      .registerInto(catalog);

  // ---- primitive id channel -------------------------------------------------
  makeTest("frame", "frame_primitiveID_channel")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        GeometryOptions o;
        o.subtype = "triangle";
        o.shape = "triangle";
        o.primitiveCount = 8;
        o.seed = 12345;
        auto geom = buildGeometry(d, o);
        std::vector<uint32_t> ids = {5, 3, 4, 2, 7, 6, 0, 1};
        anari::setAndReleaseParameter(
            d, geom, "primitive.id", anari::newArray1D(d, ids.data(), ids.size()));
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
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        auto s0 = idSurface(d, 12345, 1, true);
        auto s1 = idSurface(d, 54321, 0, true);
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
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        auto i0 = idInstance(d, 12345, 0, false);
        auto i1 = idInstance(d, 54321, 0, false);
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
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        auto makeVol = [&](int seed, uint32_t id) {
          auto field = makeStructuredRegularField(d, {3, 3, 3}, seed);
          auto vol = anari::newObject<anari::Volume>(d, "scivis");
          anari::setParameter(d, vol, "value", field);
          std::vector<float3> col = {{0.f, 0.f, 1.f}, {1.f, 0.f, 0.f}};
          std::vector<float> op = {0.f, 1.f};
          anari::setAndReleaseParameter(
              d, vol, "color", anari::newArray1D(d, col.data(), col.size()));
          anari::setAndReleaseParameter(
              d, vol, "opacity", anari::newArray1D(d, op.data(), op.size()));
          anari::setParameter(d, vol, "id", id);
          anari::commitParameters(d, vol);
          anari::release(d, field);
          return vol;
        };
        auto v0 = makeVol(12345, 1);
        auto v1 = makeVol(54321, 0);
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
          "ANARI_KHR_VOLUME_SCIVIS"})
      .registerInto(catalog);

  // ---- instance id channel --------------------------------------------------
  makeTest("frame", "frame_instanceID_channel")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        auto i0 = idInstance(d, 12345, 1, true);
        auto i1 = idInstance(d, 54321, 0, true);
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

  // ---- behavioral checks (corpus completeness; not verified by comparison) --
  makeTest("frame", "frame_completion_callback")
      .build([](BuildContext &ctx) {
        return triangleSurfaceWorld(ctx, float3(0.7f, 0.5f, 0.3f), 1);
      })
      .requireFeature("ANARI_KHR_FRAME_COMPLETION_CALLBACK")
      .registerInto(catalog);

  makeTest("frame", "progressive_rendering")
      .build([](BuildContext &ctx) {
        return triangleSurfaceWorld(ctx, float3(0.7f, 0.5f, 0.3f), 1);
      })
      .requireFeature("ANARI_KHR_PROGRESSIVE_RENDERING")
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
