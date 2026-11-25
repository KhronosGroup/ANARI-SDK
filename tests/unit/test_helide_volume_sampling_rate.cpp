// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Regression for helide's renderer 'volumeSamplingRate': it defaults to 0.5,
// the default HelideDefinitions.json advertises, and is clamped to the
// declared range [0.001, 10].

#include "catch.hpp"
#include "helide_render_test.h"

// std
#include <optional>

namespace {

using namespace helide_test;

// Renders a world holding one translucent volume filling [-1, 1]^3, with the
// renderer's 'volumeSamplingRate' set to 'rate' (left unset if empty).
RenderResult renderWithRate(
    anari::Device d, std::optional<float> rate = std::nullopt)
{
  auto volume = makeVolume(d, float3(-1.f), float3(1.f), float3(1.f), 1);
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "volume", &volume, 1);
  anari::commitParameters(d, world);
  anari::release(d, volume);

  RendererSetup setup;
  if (rate) {
    setup = [rate](anari::Device d, anari::Renderer r) {
      anari::setParameter(d, r, "volumeSamplingRate", *rate);
    };
  }
  auto result = renderChannels(d, world, "default", setup);
  anari::release(d, world);
  return result;
}

} // namespace

TEST_CASE("helide's volumeSamplingRate defaults to 0.5 and is clamped",
    "[helide][helide_volume_sampling_rate]")
{
  anari::Library lib = anari::loadLibrary("helide", printErrors, nullptr);
  if (lib == nullptr) {
    WARN("helide library not available; skipping volumeSamplingRate test");
    return;
  }

  anari::Device d = anari::newDevice(lib, "default");

  const size_t pixels = size_t(kImageSize.x) * kImageSize.y;

  SECTION("the default matches an explicit 0.5")
  {
    const auto half = renderWithRate(d, 0.5f);
    // The equivalence means something only if the rate changes the image.
    CHECK(
        countMismatches(half.color, renderWithRate(d, 1.f).color) > pixels / 4);
    checkSameImage(renderWithRate(d), half);
  }

  SECTION("rates below 0.001 are clamped to 0.001")
  {
    const auto lowest = renderWithRate(d, 0.001f);
    CHECK(countId(lowest.objectId, 1) > pixels / 2);
    checkSameImage(renderWithRate(d, 0.f), lowest);
    checkSameImage(renderWithRate(d, -1.f), lowest);
  }

  SECTION("rates above 10 are clamped to 10")
  {
    const auto highest = renderWithRate(d, 10.f);
    CHECK(countMismatches(highest.color, renderWithRate(d, 5.f).color) > 0);
    checkSameImage(renderWithRate(d, 20.f), highest);
  }

  anari::release(d, d);
  anari::unloadLibrary(lib);
}
