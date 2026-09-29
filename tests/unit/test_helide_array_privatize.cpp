// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Regression for privatizing a shared array in helide: once the app releases
// a triangle geometry's shared 'vertex.position' array, it may overwrite or
// free its buffer, and later renders (and a render already in flight) must
// keep drawing the vertices it had when released. Each case changes the
// surface before rendering again, so helide rebuilds its Embree scenes from the
// geometry's vertex buffer rather than reusing the BVH built before the
// release.

#include "catch.hpp"
#include "helide_render_test.h"

// std
#include <algorithm>
#include <memory>

using namespace helide_test;

SCENARIO("helide keeps rendering a shared array the app released",
    "[helide_array_privatize]")
{
  anari::Library lib = anari::loadLibrary("helide", printErrors, nullptr);
  if (lib == nullptr) {
    WARN("helide library not available; skipping privatize test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");

  auto vertices = std::make_unique<float3[]>(3);
  std::copy(kCenterTriangle, kCenterTriangle + 3, vertices.get());
  auto [world, surface, positions, geometry] =
      makeTriangleWorld(d, vertices.get());
  const auto expected = renderChannels(d, world);
  REQUIRE(countHits(expected.depth) > 0);

  GIVEN("the app releases the vertex array and overwrites its buffer")
  {
    anari::release(d, positions);
    moveOutOfView(vertices.get());

    THEN("the next render, after a scene rebuild, is unchanged")
    {
      forceSceneRebuild(d, surface);
      checkSameImage(renderChannels(d, world), expected);
    }
  }

  GIVEN("the app releases the vertex array, overwrites and frees its buffer")
  {
    anari::release(d, positions);
    moveOutOfView(vertices.get());
    vertices.reset();

    THEN("the next render, after a scene rebuild, is unchanged")
    {
      forceSceneRebuild(d, surface);
      checkSameImage(renderChannels(d, world), expected);
    }
  }

  GIVEN("the app releases the vertex array while a render is in flight")
  {
    auto frame = newFrame(d, world);
    anari::render(d, frame);
    anari::release(d, positions);
    moveOutOfView(vertices.get());
    vertices.reset();

    THEN("the in-flight render and the next render are unchanged")
    {
      checkSameImage(readChannels(d, frame), expected);
      forceSceneRebuild(d, surface);
      anari::render(d, frame);
      checkSameImage(readChannels(d, frame), expected);
    }

    anari::release(d, frame);
  }

  anari::release(d, surface);
  anari::release(d, world);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}
