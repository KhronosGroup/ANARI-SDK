// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Regression for helide's transferFunction1D opacity: the color's alpha scales
// the opacity exactly once, whether the opacity is a uniform value or an array.
// So a uniform color with alpha 0.5 and opacity 1 renders like an opaque color
// with opacity 0.5, and like the same color with an array of 1s as opacity.

#include "catch.hpp"
#include "helide_render_test.h"

// std
#include <vector>

namespace {

using namespace helide_test;

constexpr uint32_t kVolumeId = 1;

// An uncommitted transferFunction1D volume filling [-1, 1]^3 with red of the
// given 'alpha'; the caller sets its opacity.
anari::Volume makeRedVolume(anari::Device d, float alpha)
{
  auto volume = anari::newObject<anari::Volume>(d, "transferFunction1D");
  anari::setAndReleaseParameter(
      d, volume, "value", makeConstantField(d, float3(-1.f), float3(1.f)));
  anari::setParameter(d, volume, "color", float4(1.f, 0.f, 0.f, alpha));
  anari::setParameter(d, volume, "id", kVolumeId);
  return volume;
}

// Renders a world holding only 'volume'.
RenderResult renderVolume(anari::Device d, anari::Volume volume)
{
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "volume", &volume, 1);
  anari::commitParameters(d, world);
  auto result = renderChannels(d, world);
  anari::release(d, world);
  return result;
}

// Renders a red volume of the given 'alpha' and uniform 'opacity'.
RenderResult renderUniformOpacity(anari::Device d, float alpha, float opacity)
{
  auto volume = makeRedVolume(d, alpha);
  anari::setParameter(d, volume, "opacity", opacity);
  anari::commitParameters(d, volume);
  auto result = renderVolume(d, volume);
  anari::release(d, volume);
  return result;
}

// Renders a red volume of the given 'alpha' and an opacity array of all 1s.
RenderResult renderOpaqueOpacityArray(anari::Device d, float alpha)
{
  const std::vector<float> opacity = {1.f, 1.f};
  auto volume = makeRedVolume(d, alpha);
  anari::setParameterArray1D(
      d, volume, "opacity", opacity.data(), opacity.size());
  anari::commitParameters(d, volume);
  auto result = renderVolume(d, volume);
  anari::release(d, volume);
  return result;
}

} // namespace

TEST_CASE("helide applies a transferFunction1D color's alpha to opacity once",
    "[helide][helide_tf1d_opacity]")
{
  anari::Library lib = anari::loadLibrary("helide", printErrors, nullptr);
  if (lib == nullptr) {
    WARN("helide library not available; skipping tf1d opacity test");
    return;
  }

  anari::Device d = anari::newDevice(lib, "default");

  const size_t pixels = size_t(kImageSize.x) * kImageSize.y;

  const auto halfAlpha = renderUniformOpacity(d, 0.5f, 1.f);
  const auto halfOpacity = renderUniformOpacity(d, 1.f, 0.5f);
  const auto halfAlphaArray = renderOpaqueOpacityArray(d, 0.5f);
  const auto fullOpacity = renderUniformOpacity(d, 1.f, 1.f);

  CHECK(countId(halfOpacity.objectId, kVolumeId) > pixels / 2);
  // The equivalence below means something only if opacity changes the image.
  CHECK(countMismatches(halfOpacity.color, fullOpacity.color) > pixels / 2);

  SECTION("uniform alpha 0.5 matches uniform opacity 0.5")
  {
    checkSameImage(halfAlpha, halfOpacity);
  }

  SECTION("uniform alpha 0.5 matches alpha 0.5 with an array of opacity 1")
  {
    checkSameImage(halfAlpha, halfAlphaArray);
  }

  anari::release(d, d);
  anari::unloadLibrary(lib);
}
