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
#include <memory>
#include <vector>

namespace {

using namespace helide_test;

// One triangle covering the center of the view.
const float3 kTriangle[3] = {
    {-0.5f, -0.5f, 0.f}, {0.5f, -0.5f, 0.f}, {0.f, 0.5f, 0.f}};

// A world holding one surface with a triangle geometry whose
// 'vertex.position' is a shared array over 'vertices' (3 elements).
struct TriangleWorld
{
  anari::World world{nullptr};
  anari::Surface surface{nullptr};
  anari::Array1D positions{nullptr};
};

TriangleWorld makeTriangleWorld(anari::Device d, const float3 *vertices)
{
  auto positions = anari::newArray1D(d, vertices, 3);

  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameter(d, geom, "vertex.position", positions);
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::commitParameters(d, world);
  return {world, surface, positions};
}

// Moves the triangle in 'vertices' out of view.
void scribble(float3 *vertices)
{
  for (int i = 0; i < 3; i++)
    vertices[i] = float3(100.f, 100.f, 100.f + i);
}

// Changes 'surface' (sets its default 'visible' explicitly) and commits it,
// which makes helide rebuild its Embree scenes from the geometry buffers at the
// next render.
void recommit(anari::Device d, anari::Surface surface)
{
  anari::setParameter(d, surface, "visible", true);
  anari::commitParameters(d, surface);
}

} // namespace

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
  std::copy(kTriangle, kTriangle + 3, vertices.get());
  auto [world, surface, positions] = makeTriangleWorld(d, vertices.get());
  const auto expected = renderChannels(d, world);
  REQUIRE(countHits(expected.depth) > 0);

  GIVEN("the app releases the vertex array and overwrites its buffer")
  {
    anari::release(d, positions);
    scribble(vertices.get());

    THEN("the next render, after a scene rebuild, is unchanged")
    {
      recommit(d, surface);
      checkSameImage(renderChannels(d, world), expected);
    }
  }

  GIVEN("the app releases the vertex array, overwrites and frees its buffer")
  {
    anari::release(d, positions);
    scribble(vertices.get());
    vertices.reset();

    THEN("the next render, after a scene rebuild, is unchanged")
    {
      recommit(d, surface);
      checkSameImage(renderChannels(d, world), expected);
    }
  }

  GIVEN("the app releases the vertex array while a render is in flight")
  {
    auto frame = newFrame(d, world);
    anari::render(d, frame);
    anari::release(d, positions);
    scribble(vertices.get());
    vertices.reset();

    THEN("the in-flight render and the next render are unchanged")
    {
      checkSameImage(readChannels(d, frame), expected);
      recommit(d, surface);
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
