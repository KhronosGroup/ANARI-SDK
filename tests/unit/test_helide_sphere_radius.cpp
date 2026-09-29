// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Regression for helide's sphere 'radius' default: a sphere with neither
// 'radius' nor 'vertex.radius' has radius 1.0, as khr_geometry_sphere says and
// as helide's parameter introspection reports.

#include "catch.hpp"
#include "helide_render_test.h"

using namespace helide_test;

TEST_CASE("helide sphere 'radius' defaults to 1.0", "[helide_sphere_radius]")
{
  anari::Library lib = anari::loadLibrary("helide", printErrors);
  if (lib == nullptr) {
    WARN("helide library not available; skipping sphere radius test");
    return;
  }

  anari::Device d = anari::newDevice(lib, "default");

  SECTION("introspection reports 1.0")
  {
    const auto *def = static_cast<const float *>(anariGetParameterInfo(d,
        ANARI_GEOMETRY,
        "sphere",
        "radius",
        ANARI_FLOAT32,
        "default",
        ANARI_FLOAT32));
    REQUIRE(def != nullptr);
    CHECK(*def == 1.f);
  }

  SECTION("a sphere without a radius has radius 1.0")
  {
    auto geom = anari::newObject<anari::Geometry>(d, "sphere");
    const float3 center(2.f, 0.f, 0.f);
    anari::setParameterArray1D(d, geom, "vertex.position", &center, 1);
    anari::commitParameters(d, geom);

    auto world = makeSurfaceWorld(d, geom);

    Box bounds{float3(0.f), float3(0.f)};
    REQUIRE(queryBounds(d, world, bounds));
    for (int a = 0; a < 3; a++) {
      CHECK(bounds.lower[a] == Approx(center[a] - 1.f).margin(1e-4));
      CHECK(bounds.upper[a] == Approx(center[a] + 1.f).margin(1e-4));
    }

    anari::release(d, world);
  }

  anari::release(d, d);
  anari::unloadLibrary(lib);
}
