// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Regression for helide's choice of the volume a ray hits and the interval it
// marches: each volume's interval is clipped to [0, surface tfar] before the
// empty test, the nearest non-empty one is kept (the first on ties), and it is
// marched over its whole clipped interval. So a volume behind the camera or
// behind an opaque surface is a miss, and adding a farther (or tied, later)
// volume doesn't change the image.

#include "catch.hpp"
#include "helide_render_test.h"

// std
#include <vector>

namespace {

using namespace helide_test;

constexpr uint32_t kVolumeId = 1;
constexpr uint32_t kOtherVolumeId = 2;
constexpr uint32_t kSurfaceId = 3;

// An opaque quad covering the view at z = 0.
anari::Surface makeWall(anari::Device d)
{
  const std::vector<float3> positions = {
      {-3.f, -3.f, 0.f}, {3.f, -3.f, 0.f}, {3.f, 3.f, 0.f}, {-3.f, 3.f, 0.f}};
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};

  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(
      d, geom, "vertex.position", positions.data(), positions.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::setParameter(d, surface, "id", kSurfaceId);
  anari::commitParameters(d, surface);
  return surface;
}

RenderResult renderWorld(anari::Device d,
    const std::vector<anari::Volume> &volumes,
    anari::Surface surface = nullptr)
{
  auto world = anari::newObject<anari::World>(d);
  if (!volumes.empty()) {
    anari::setParameterArray1D(
        d, world, "volume", volumes.data(), volumes.size());
  }
  if (surface)
    anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::commitParameters(d, world);
  auto result = renderChannels(d, world);
  anari::release(d, world);
  return result;
}

} // namespace

TEST_CASE("helide selects the nearest non-empty volume interval",
    "[helide][helide_volume_interval]")
{
  anari::Library lib = anari::loadLibrary("helide", printErrors, nullptr);
  if (lib == nullptr) {
    WARN("helide library not available; skipping volume interval test");
    return;
  }

  anari::Device d = anari::newDevice(lib, "default");

  const size_t pixels = size_t(kImageSize.x) * kImageSize.y;

  // The camera is at z = 2 looking down -z.

  SECTION("a volume behind the camera is a miss")
  {
    auto behind = makeVolume(
        d, {-1.f, -1.f, 2.5f}, {1.f, 1.f, 3.5f}, {1.f, 0.f, 0.f}, kVolumeId);
    const auto withVolume = renderWorld(d, {behind});
    const auto empty = renderWorld(d, {});
    CHECK(countId(withVolume.objectId, kVolumeId) == 0);
    checkSameImage(withVolume, empty);
    anari::release(d, behind);
  }

  SECTION("a volume behind an opaque surface is a miss")
  {
    auto wall = makeWall(d);
    auto behind = makeVolume(
        d, {-1.f, -1.f, -2.f}, {1.f, 1.f, -1.f}, {1.f, 0.f, 0.f}, kVolumeId);
    const auto withVolume = renderWorld(d, {behind}, wall);
    const auto wallOnly = renderWorld(d, {}, wall);
    CHECK(countId(wallOnly.objectId, kSurfaceId) == pixels);
    CHECK(countId(withVolume.objectId, kVolumeId) == 0);
    checkSameImage(withVolume, wallOnly);
    anari::release(d, behind);
    anari::release(d, wall);
  }

  SECTION("the nearer of two overlapping volumes is marched over its interval")
  {
    // 'near' is entered first (z = 0.5) but listed second; 'far' (entered at
    // z = 0.25) must not clip its interval.
    auto near = makeVolume(
        d, {-1.f, -1.f, -0.5f}, {1.f, 1.f, 0.5f}, {1.f, 0.f, 0.f}, kVolumeId);
    auto far = makeVolume(d,
        {-1.f, -1.f, -1.f},
        {1.f, 1.f, 0.25f},
        {0.f, 0.f, 1.f},
        kOtherVolumeId);
    const auto both = renderWorld(d, {far, near});
    const auto nearOnly = renderWorld(d, {near});
    CHECK(countId(nearOnly.objectId, kVolumeId) > pixels / 2);
    checkSameImage(both, nearOnly);
    anari::release(d, near);
    anari::release(d, far);
  }

  SECTION("the nearer volume is marched up to the surface hit")
  {
    // The wall at z = 0 cuts 'near' (and 'far', listed first and entered
    // behind 'near', which must not clip 'near' further).
    auto wall = makeWall(d);
    auto near = makeVolume(
        d, {-1.f, -1.f, -0.5f}, {1.f, 1.f, 0.5f}, {1.f, 0.f, 0.f}, kVolumeId);
    auto far = makeVolume(d,
        {-1.f, -1.f, -1.f},
        {1.f, 1.f, 0.25f},
        {0.f, 0.f, 1.f},
        kOtherVolumeId);
    const auto both = renderWorld(d, {far, near}, wall);
    const auto nearOnly = renderWorld(d, {near}, wall);
    CHECK(countId(nearOnly.objectId, kVolumeId) > pixels / 2);
    checkSameImage(both, nearOnly);
    anari::release(d, near);
    anari::release(d, far);
    anari::release(d, wall);
  }

  SECTION("of two volumes containing the camera, the first listed is hit")
  {
    auto first = makeVolume(
        d, {-1.f, -1.f, -0.5f}, {1.f, 1.f, 2.5f}, {1.f, 0.f, 0.f}, kVolumeId);
    auto second = makeVolume(d,
        {-1.f, -1.f, -1.f},
        {1.f, 1.f, 3.f},
        {0.f, 0.f, 1.f},
        kOtherVolumeId);
    const auto both = renderWorld(d, {first, second});
    const auto firstOnly = renderWorld(d, {first});
    const auto secondOnly = renderWorld(d, {second});
    CHECK(countMismatches(firstOnly.color, secondOnly.color) > pixels / 2);
    checkSameImage(both, firstOnly);
    anari::release(d, first);
    anari::release(d, second);
  }

  anari::release(d, d);
  anari::unloadLibrary(lib);
}
