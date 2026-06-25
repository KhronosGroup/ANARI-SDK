// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "BuiltinTests.h"
#include "BuildContext.h"
#include "TestBuilder.h"
#include "WorldBuilder.h"

namespace anari {
namespace cts {

namespace {

using anari::math::float3;

// Author a single-surface world from a geometry of the requested subtype,
// driven by the Case's axis values.
anari::World buildGeometryWorld(BuildContext &ctx, const char *subtype)
{
  auto d = ctx.device();

  GeometryOptions opts;
  opts.subtype = subtype;
  opts.primitiveCount = ctx.get<int>("primitiveCount", 16);
  opts.primitiveMode = ctx.getString("primitiveMode", "soup");
  opts.seed = ctx.get<int>("seed", 0);

  auto geom = buildGeometry(d, opts);
  auto material = makeMatteMaterial(d, float3(0.7f, 0.5f, 0.3f));
  auto surface = makeSurface(d, geom, material);
  auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));

  WorldContents contents;
  contents.surfaces = {surface};
  contents.lights = {light};
  auto world = assembleWorld(d, contents);

  anari::release(d, geom);
  anari::release(d, material);
  anari::release(d, surface);
  anari::release(d, light);
  return world;
}

} // namespace

void registerBuiltinTests(Catalog &catalog)
{
  // soup vs indexed must render identically -> a Variant sharing ground truth.
  makeTest("geometry", "triangle")
      .build([](BuildContext &ctx) { return buildGeometryWorld(ctx, "triangle"); })
      .permute("primitiveCount", {16, 64})
      .variant("primitiveMode", {"soup", "indexed"})
      .requireFeatures({"ANARI_KHR_GEOMETRY_TRIANGLE", "ANARI_KHR_MATERIAL_MATTE"})
      .registerInto(catalog);

  makeTest("geometry", "sphere")
      .build([](BuildContext &ctx) { return buildGeometryWorld(ctx, "sphere"); })
      .permute("primitiveCount", {16})
      .variant("primitiveMode", {"soup", "indexed"})
      .channels({Channel::Color, Channel::Depth})
      .requireFeatures({"ANARI_KHR_GEOMETRY_SPHERE", "ANARI_KHR_MATERIAL_MATTE"})
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
