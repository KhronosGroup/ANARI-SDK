// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "VolumeBuilder.h"
// std
#include <vector>

namespace anari {
namespace cts {

float radialDistanceField(math::float3 p)
{
  return math::length(p);
}

anari::SpatialField newStructuredRegularField(
    anari::Device d, std::array<uint32_t, 3> dimensions, const FieldFn &field)
{
  auto spatialField =
      anari::newObject<anari::SpatialField>(d, "structuredRegular");
  const FieldFn &function = field ? field : FieldFn(radialDistanceField);
  auto coordinate = [](uint32_t index, uint32_t dimension) {
    return dimension > 1
        ? 2.f * static_cast<float>(index) / static_cast<float>(dimension - 1)
            - 1.f
        : 0.f;
  };

  std::vector<float> data;
  data.reserve(
      static_cast<size_t>(dimensions[0]) * dimensions[1] * dimensions[2]);
  for (uint32_t z = 0; z < dimensions[2]; ++z) {
    for (uint32_t y = 0; y < dimensions[1]; ++y) {
      for (uint32_t x = 0; x < dimensions[0]; ++x) {
        data.push_back(function(math::float3(coordinate(x, dimensions[0]),
            coordinate(y, dimensions[1]),
            coordinate(z, dimensions[2]))));
      }
    }
  }

  anari::setParameter(d, spatialField, "origin", math::float3(-1.f));
  anari::setParameter(d,
      spatialField,
      "spacing",
      math::float3(dimensions[0] ? 2.f / dimensions[0] : 0.f,
          dimensions[1] ? 2.f / dimensions[1] : 0.f,
          dimensions[2] ? 2.f / dimensions[2] : 0.f));
  anari::setParameterArray3D(d,
      spatialField,
      "data",
      data.data(),
      dimensions[0],
      dimensions[1],
      dimensions[2]);
  return spatialField;
}

anari::SpatialField makeStructuredRegularField(
    anari::Device d, std::array<uint32_t, 3> dimensions, const FieldFn &field)
{
  auto spatialField = newStructuredRegularField(d, dimensions, field);
  anari::commitParameters(d, spatialField);
  return spatialField;
}

anari::Volume makeVolume(anari::Device d, anari::SpatialField field)
{
  auto volume = anari::newObject<anari::Volume>(d, "transferFunction1D");
  anari::setParameter(d, volume, "value", field);
  std::vector<math::float3> colors = {
      {0.f, 0.f, 1.f}, {0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}};
  std::vector<float> opacities = {0.f, 1.f};
  anari::setAndReleaseParameter(
      d, volume, "color", anari::newArray1D(d, colors.data(), colors.size()));
  anari::setAndReleaseParameter(d,
      volume,
      "opacity",
      anari::newArray1D(d, opacities.data(), opacities.size()));
  anari::setParameter(d, volume, "valueRange", math::float2(0.f, 1.f));
  anari::commitParameters(d, volume);
  return volume;
}

} // namespace cts
} // namespace anari
