// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Volume conformance test, migrated from cts/test_scenes/volume/volume.json: a
// transferFunction1D volume over a structuredRegular field, permuting the
// field's filter/origin/spacing and the volume's color/opacity/valueRange/
// unitDistance one-factor-at-a-time. Array-vs-constant color and opacity are
// selected by per-Case tokens.

#include "Categories.h"
#include "../BuildContext.h"
#include "../TestBuilder.h"
#include "../WorldBuilder.h"
// std
#include <string>
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float3;
using anari::math::float4;
using V = std::vector<Any>;

void setVolumeColor(BuildContext &ctx, anari::Device d, anari::Volume vol)
{
  const std::string c = ctx.getString("color", "array");
  if (c == "vec3") {
    anari::setParameter(d, vol, "color", float3(1.f, 0.75f, 0.f));
  } else if (c == "vec4") {
    anari::setParameter(d, vol, "color", float4(1.f, 0.75f, 0.f, 0.5f));
  } else {
    std::vector<float3> cols = {
        {0.25f, 0.9f, 0.2f}, {0.9f, 0.1f, 0.f}, {0.f, 0.f, 0.7f}};
    anari::setAndReleaseParameter(
        d, vol, "color", anari::newArray1D(d, cols.data(), cols.size()));
  }
}

void setVolumeOpacity(BuildContext &ctx, anari::Device d, anari::Volume vol)
{
  const std::string o = ctx.getString("opacity", "array");
  if (o == "scalar") {
    anari::setParameter(d, vol, "opacity", 1.0f);
  } else {
    std::vector<float> ops = {0.25f, 0.7f, 1.0f};
    anari::setAndReleaseParameter(
        d, vol, "opacity", anari::newArray1D(d, ops.data(), ops.size()));
  }
}

} // namespace

void registerVolumeTests(Catalog &catalog)
{
  makeTest("volume", "volume")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        auto field = newStructuredRegularField(d, {3, 3, 3});
        setBoundParameter(d, field, "filter", ctx.value("filter"));
        setBoundParameter(d, field, "origin", ctx.value("origin"));
        setBoundParameter(d, field, "spacing", ctx.value("spacing"));
        anari::commitParameters(d, field);

        auto vol = anari::newObject<anari::Volume>(d, "transferFunction1D");
        anari::setParameter(d, vol, "value", field);
        setVolumeColor(ctx, d, vol);
        setVolumeOpacity(ctx, d, vol);
        setBoundParameter(d, vol, "valueRange", ctx.value("valueRange"));
        setBoundParameter(d, vol, "unitDistance", ctx.value("unitDistance"));
        anari::commitParameters(d, vol);

        WorldContents wc;
        wc.volumes = {vol};
        auto world = assembleWorld(d, wc);
        anari::release(d, field);
        anari::release(d, vol);
        return world;
      })
      .simplified()
      .permute("filter", V{none(), Any("nearest"), Any("linear")})
      .permute("origin", V{Any(float3(-1.f, -1.f, -1.f)), none()})
      .permute("spacing", V{none(), Any(float3(0.01f, 0.02f, 0.5f))})
      .permute("valueRange", V{none(), Any(anari::math::float2(0.25f, 0.75f))})
      .permute("color", {"array", "vec3", "vec4"})
      .permute("opacity", {"array", "scalar"})
      .permute("unitDistance", V{none(), Any(0.1f)})
      .channels({Channel::Color, Channel::Depth})
      .requireFeatures({"ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR",
          "ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D"})
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
