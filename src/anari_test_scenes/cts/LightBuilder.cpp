// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "LightBuilder.h"
#include "generators/TextureGenerator.h"

namespace anari {
namespace cts {

anari::Light makeDirectionalLight(
    anari::Device d, math::float3 direction, float irradiance)
{
  auto light = anari::newObject<anari::Light>(d, "directional");
  anari::setParameter(d, light, "direction", direction);
  anari::setParameter(d, light, "irradiance", irradiance);
  anari::commitParameters(d, light);
  return light;
}

anari::Light newLight(anari::Device d, const std::string &subtype)
{
  auto light = anari::newObject<anari::Light>(d, subtype.c_str());
  if (subtype == "hdri") {
    const size_t resolution = 64;
    auto radiance =
        scenes::TextureGenerator::generateCheckerBoardHDR(resolution);
    anari::setAndReleaseParameter(d,
        light,
        "radiance",
        anari::newArray2D(d, radiance.data(), resolution, resolution));
  }
  return light;
}

} // namespace cts
} // namespace anari
