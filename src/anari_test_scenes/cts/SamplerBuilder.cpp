// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SamplerBuilder.h"
#include "generators/TextureGenerator.h"
// std
#include <stdexcept>

namespace anari {
namespace cts {

anari::Sampler makeCheckerboardSampler(anari::Device d, bool normalMap)
{
  auto sampler = anari::newObject<anari::Sampler>(d, "image2D");
  const size_t resolution = 64;
  auto image = normalMap
      ? scenes::TextureGenerator::generateCheckerBoardNormalMap(resolution)
      : scenes::TextureGenerator::generateCheckerBoard(resolution);
  anari::setAndReleaseParameter(d,
      sampler,
      "image",
      anari::newArray2D(d, image.data(), resolution, resolution));
  anari::commitParameters(d, sampler);
  return sampler;
}

anari::Sampler newImageSampler(anari::Device d,
    const std::string &subtype,
    const std::string &inAttribute,
    bool normalMap)
{
  if (subtype != "image1D" && subtype != "image2D" && subtype != "image3D") {
    throw std::invalid_argument(
        "unsupported image sampler subtype '" + subtype + "'");
  }
  if (normalMap && subtype != "image2D") {
    throw std::invalid_argument(
        "normal-map generation requires image2D sampler subtype");
  }

  auto sampler = anari::newObject<anari::Sampler>(d, subtype.c_str());
  if (subtype == "image1D") {
    auto image = scenes::TextureGenerator::generateGreyScale(16);
    anari::setAndReleaseParameter(
        d, sampler, "image", anari::newArray1D(d, image.data(), image.size()));
  } else if (subtype == "image2D") {
    const size_t resolution = 64;
    auto image = normalMap
        ? scenes::TextureGenerator::generateCheckerBoardNormalMap(resolution)
        : scenes::TextureGenerator::generateCheckerBoard(resolution);
    anari::setAndReleaseParameter(d,
        sampler,
        "image",
        anari::newArray2D(d, image.data(), resolution, resolution));
  } else {
    const size_t resolution = 32;
    auto image = scenes::TextureGenerator::generateRGBRamp(resolution);
    anari::setAndReleaseParameter(d,
        sampler,
        "image",
        anari::newArray3D(d, image.data(), resolution, resolution, resolution));
  }

  if (!inAttribute.empty())
    anari::setParameter(d, sampler, "inAttribute", inAttribute.c_str());
  return sampler;
}

} // namespace cts
} // namespace anari
