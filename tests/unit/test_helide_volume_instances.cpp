// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Regression for helide's volume intersection through instances: every
// transform of an instance's transform array is tested (not the one indexed by
// the instance's position in the world), and the instance id channel reports
// the instance (and array element) holding the hit volume, not the last
// instance listed.

#include "catch.hpp"
#include "helide_render_test.h"

// std
#include <vector>

namespace {

using namespace helide_test;

constexpr uint32_t kVolumeId = 1;

// A small volume at the origin; instances translate it into view.
anari::Group makeVolumeGroup(anari::Device d)
{
  auto volume =
      makeVolume(d, float3(-0.2f), float3(0.2f), {1.f, 0.f, 0.f}, kVolumeId);
  auto group = anari::newObject<anari::Group>(d);
  anari::setParameterArray1D(d, group, "volume", &volume, 1);
  anari::commitParameters(d, group);
  anari::release(d, volume);
  return group;
}

anari::Instance makeInstance(anari::Device d,
    anari::Group group,
    const std::vector<mat4> &xfms,
    const std::vector<uint32_t> &ids)
{
  auto inst = anari::newObject<anari::Instance>(d, "transform");
  anari::setParameter(d, inst, "group", group);
  if (xfms.size() == 1) {
    anari::setParameter(d, inst, "transform", xfms[0]);
    anari::setParameter(d, inst, "id", ids[0]);
  } else {
    // More than one transform uses a transform array (with an id per element).
    anari::setParameterArray1D(d, inst, "transform", xfms.data(), xfms.size());
    anari::setParameterArray1D(d, inst, "id", ids.data(), ids.size());
  }
  anari::commitParameters(d, inst);
  return inst;
}

RenderResult renderInstances(
    anari::Device d, const std::vector<anari::Instance> &instances)
{
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(
      d, world, "instance", instances.data(), instances.size());
  anari::commitParameters(d, world);
  auto result = renderChannels(d, world);
  anari::release(d, world);
  for (auto i : instances)
    anari::release(d, i);
  return result;
}

mat4 translationX(float x)
{
  return linalg::translation_matrix(float3(x, 0.f, 0.f));
}

} // namespace

TEST_CASE("helide intersects volumes through every instance transform",
    "[helide][helide_volume_instances]")
{
  anari::Library lib = anari::loadLibrary("helide", printErrors, nullptr);
  if (lib == nullptr) {
    WARN("helide library not available; skipping volume instance test");
    return;
  }

  anari::Device d = anari::newDevice(lib, "default");
  auto group = makeVolumeGroup(d);

  // The camera is at z = 2 looking down -z; x = +/-0.7 is in view.
  const std::vector<mat4> xfms = {
      translationX(-0.7f), translationX(0.f), translationX(0.7f)};
  const std::vector<uint32_t> ids = {10, 11, 12};

  SECTION("an instance array shows one volume per transform")
  {
    const auto array = renderInstances(d, {makeInstance(d, group, xfms, ids)});
    const auto separate = renderInstances(d,
        {makeInstance(d, group, {xfms[0]}, {ids[0]}),
            makeInstance(d, group, {xfms[1]}, {ids[1]}),
            makeInstance(d, group, {xfms[2]}, {ids[2]})});
    for (auto id : ids)
      CHECK(countId(separate.instanceId, id) > 0);
    checkSameImage(array, separate);
  }

  SECTION("the instance id is the one holding the hit volume")
  {
    // The second instance's volume is out of view, so no ray hits it.
    const auto both = renderInstances(d,
        {makeInstance(d, group, {xfms[1]}, {ids[1]}),
            makeInstance(d, group, {translationX(100.f)}, {99})});
    const auto firstOnly =
        renderInstances(d, {makeInstance(d, group, {xfms[1]}, {ids[1]})});
    CHECK(countId(both.instanceId, 99) == 0);
    CHECK(countId(both.instanceId, ids[1]) > 0);
    checkSameImage(both, firstOnly);
  }

  anari::release(d, group);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}
