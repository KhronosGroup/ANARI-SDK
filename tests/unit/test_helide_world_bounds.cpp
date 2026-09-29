// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Regression for helide's world "bounds" property with instanced volumes: each
// volume's box is transformed by every transform of its instance, and
// invisible volumes are left out (as the group's bounds already do).

#include "catch.hpp"
#include "helide_render_test.h"

// std
#include <vector>

namespace {

using namespace helide_test;

struct Box
{
  float3 lower;
  float3 upper;
};

// A group holding one volume filling ['lower', 'upper'].
anari::Group makeVolumeGroup(anari::Device d,
    const float3 &lower,
    const float3 &upper,
    bool visible = true)
{
  auto volume = makeVolume(d, lower, upper, {1.f, 0.f, 0.f}, 1);
  if (!visible) {
    anari::setParameter(d, volume, "visible", false);
    anari::commitParameters(d, volume);
  }
  auto group = anari::newObject<anari::Group>(d);
  anari::setParameterArray1D(d, group, "volume", &volume, 1);
  anari::commitParameters(d, group);
  anari::release(d, volume);
  return group;
}

anari::Instance makeInstance(
    anari::Device d, anari::Group group, const std::vector<mat4> &xfms)
{
  auto inst = anari::newObject<anari::Instance>(d, "transform");
  anari::setParameter(d, inst, "group", group);
  if (xfms.size() == 1)
    anari::setParameter(d, inst, "transform", xfms[0]);
  else
    anari::setParameterArray1D(d, inst, "transform", xfms.data(), xfms.size());
  anari::commitParameters(d, inst);
  return inst;
}

// The world bounds of a world holding 'instances' (released here).
Box worldBounds(anari::Device d, const std::vector<anari::Instance> &instances)
{
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(
      d, world, "instance", instances.data(), instances.size());
  anari::commitParameters(d, world);
  Box bounds{float3(0.f), float3(0.f)};
  REQUIRE(anariGetProperty(d,
      world,
      "bounds",
      ANARI_FLOAT32_BOX3,
      &bounds,
      sizeof(bounds),
      ANARI_WAIT));
  anari::release(d, world);
  for (auto i : instances)
    anari::release(d, i);
  return bounds;
}

void checkBounds(const Box &actual, const Box &expected)
{
  for (int i = 0; i < 3; i++) {
    CHECK(actual.lower[i] == Approx(expected.lower[i]).margin(1e-5f));
    CHECK(actual.upper[i] == Approx(expected.upper[i]).margin(1e-5f));
  }
}

mat4 translation(const float3 &t)
{
  return linalg::translation_matrix(t);
}

} // namespace

TEST_CASE("helide world bounds transform instanced volumes",
    "[helide][helide_world_bounds]")
{
  anari::Library lib = anari::loadLibrary("helide", printErrors, nullptr);
  if (lib == nullptr) {
    WARN("helide library not available; skipping world bounds test");
    return;
  }

  anari::Device d = anari::newDevice(lib, "default");

  SECTION("a translated instance moves the volume's box")
  {
    auto group = makeVolumeGroup(d, float3(-0.2f), float3(0.2f));
    const auto bounds = worldBounds(
        d, {makeInstance(d, group, {translation({5.f, 0.f, 0.f})})});
    checkBounds(bounds, {{4.8f, -0.2f, -0.2f}, {5.2f, 0.2f, 0.2f}});
    anari::release(d, group);
  }

  SECTION("a rotated instance bounds the rotated box's corners")
  {
    // A 90 degree turn about z maps x extent [0, 2] to y extent [0, 2].
    auto group = makeVolumeGroup(d, {0.f, -0.5f, -0.5f}, {2.f, 0.5f, 0.5f});
    const mat4 rotZ = linalg::rotation_matrix(linalg::rotation_quat(
        float3(0.f, 0.f, 1.f), 1.57079632679f /* pi / 2 */));
    const auto bounds = worldBounds(d, {makeInstance(d, group, {rotZ})});
    checkBounds(bounds, {{-0.5f, 0.f, -0.5f}, {0.5f, 2.f, 0.5f}});
    anari::release(d, group);
  }

  SECTION("every transform of an instance array counts")
  {
    auto group = makeVolumeGroup(d, float3(-0.2f), float3(0.2f));
    const auto bounds = worldBounds(d,
        {makeInstance(d,
            group,
            {translation({-3.f, 0.f, 0.f}), translation({0.f, 4.f, 0.f})})});
    checkBounds(bounds, {{-3.2f, -0.2f, -0.2f}, {0.2f, 4.2f, 0.2f}});
    anari::release(d, group);
  }

  SECTION("invisible volumes are left out")
  {
    auto visible = makeVolumeGroup(d, float3(-0.2f), float3(0.2f));
    auto hidden = makeVolumeGroup(d, float3(-0.2f), float3(0.2f), false);
    const auto bounds = worldBounds(d,
        {makeInstance(d, visible, {translation({1.f, 0.f, 0.f})}),
            makeInstance(d, hidden, {translation({9.f, 0.f, 0.f})})});
    checkBounds(bounds, {{0.8f, -0.2f, -0.2f}, {1.2f, 0.2f, 0.2f}});
    anari::release(d, visible);
    anari::release(d, hidden);
  }

  anari::release(d, d);
  anari::unloadLibrary(lib);
}
