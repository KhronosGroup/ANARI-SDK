// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Regression for helide's camera rays: each ray passes through its pixel's
// centre, not its lower-left corner. An orthographic camera maps one world
// unit to one pixel, so a strip half a pixel wide centred on a pixel's centre
// covers exactly that pixel's column (or row), and a background image the
// size of the frame is read back unfiltered.

#include "catch.hpp"
#include "helide_render_test.h"

// std
#include <vector>

namespace {

using namespace helide_test;

// An 8x8 frame over the square [-4, 4]^2 of the z = 0 plane: pixel (x, y)
// covers [x - 4, x - 3] x [y - 4, y - 3], so its centre is at
// (x - 3.5, y - 3.5).
constexpr uint32_t kSize = 8;

anari::Frame newOrthoFrame(
    anari::Device d, anari::World world, anari::Array2D background = nullptr)
{
  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 2.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::setParameter(d, camera, "height", float(kSize));
  anari::commitParameters(d, camera);

  auto renderer = anari::newObject<anari::Renderer>(d, "default");
  if (background)
    anari::setParameter(d, renderer, "background", background);
  else
    anari::setParameter(d, renderer, "background", float4(0.f, 0.f, 0.f, 1.f));
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", uint2(kSize));
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "channel.depth", ANARI_FLOAT32);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);
  return frame;
}

// A world holding one quad in the z = 0 plane spanning [lo.x, hi.x] x
// [lo.y, hi.y].
anari::World makeQuadWorld(anari::Device d, const float2 &lo, const float2 &hi)
{
  const std::vector<float3> positions = {float3(lo.x, lo.y, 0.f),
      float3(hi.x, lo.y, 0.f),
      float3(hi.x, hi.y, 0.f),
      float3(lo.x, hi.y, 0.f)};
  const std::vector<uint3> indices = {uint3(0, 1, 2), uint3(0, 2, 3)};

  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(
      d, geom, "vertex.position", positions.data(), positions.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(
      d, surface, "material", anari::newObject<anari::Material>(d, "matte"));
  anari::commitParameters(d, surface);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::commitParameters(d, world);
  anari::release(d, surface);
  return world;
}

// Renders 'world' and returns, per pixel (row-major from the bottom row),
// whether its ray hit something.
std::vector<bool> renderHits(anari::Device d, anari::World world)
{
  auto frame = newOrthoFrame(d, world);
  anari::render(d, frame);
  anari::wait(d, frame);
  auto depth = anari::map<float>(d, frame, "channel.depth");
  std::vector<bool> hits(kSize * kSize);
  for (size_t i = 0; i < hits.size(); i++)
    hits[i] = isHit(depth.data[i]);
  anari::unmap(d, frame, "channel.depth");
  anari::release(d, frame);
  return hits;
}

} // namespace

TEST_CASE("helide generates camera rays through pixel centres",
    "[helide][helide_pixel_centres]")
{
  anari::Library lib = anari::loadLibrary("helide", printErrors, nullptr);
  if (lib == nullptr) {
    WARN("helide library not available; skipping pixel centre test");
    return;
  }

  anari::Device d = anari::newDevice(lib, "default");

  SECTION("a strip centred on column 4 covers exactly that column")
  {
    // Column 4's centre is at x = 0.5.
    auto world = makeQuadWorld(d, float2(0.25f, -10.f), float2(0.75f, 10.f));
    const auto hits = renderHits(d, world);
    anari::release(d, world);

    for (uint32_t y = 0; y < kSize; y++) {
      for (uint32_t x = 0; x < kSize; x++) {
        INFO("pixel (" << x << ", " << y << ")");
        CHECK(hits[y * kSize + x] == (x == 4));
      }
    }
  }

  SECTION("a strip centred on row 4 covers exactly that row")
  {
    // Row 4's centre is at y = 0.5.
    auto world = makeQuadWorld(d, float2(-10.f, 0.25f), float2(10.f, 0.75f));
    const auto hits = renderHits(d, world);
    anari::release(d, world);

    for (uint32_t y = 0; y < kSize; y++) {
      for (uint32_t x = 0; x < kSize; x++) {
        INFO("pixel (" << x << ", " << y << ")");
        CHECK(hits[y * kSize + x] == (y == 4));
      }
    }
  }

  SECTION("a frame-sized background image is read back pixel for pixel")
  {
    std::vector<float4> image(kSize * kSize);
    for (uint32_t y = 0; y < kSize; y++) {
      for (uint32_t x = 0; x < kSize; x++)
        image[y * kSize + x] =
            float4(x / float(kSize), y / float(kSize), 0.5f, 1.f);
    }
    auto background =
        anari::newArray2D(d, image.data(), size_t(kSize), size_t(kSize));

    auto world = anari::newObject<anari::World>(d);
    anari::commitParameters(d, world);
    auto frame = newOrthoFrame(d, world, background);
    anari::release(d, background);
    anari::release(d, world);

    anari::render(d, frame);
    anari::wait(d, frame);
    auto color = anari::map<float4>(d, frame, "channel.color");
    for (uint32_t y = 0; y < kSize; y++) {
      for (uint32_t x = 0; x < kSize; x++) {
        INFO("pixel (" << x << ", " << y << ")");
        const auto c = color.data[y * kSize + x];
        const auto e = image[y * kSize + x];
        CHECK(linalg::maxelem(linalg::abs(c - e)) <= 1e-5f);
      }
    }
    anari::unmap(d, frame, "channel.color");
    anari::release(d, frame);
  }

  anari::release(d, d);
  anari::unloadLibrary(lib);
}
