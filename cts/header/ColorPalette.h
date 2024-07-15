#pragma once

#include <vector>
#include "anari/anari_cpp/ext/linalg.h"

namespace colors {
extern const std::vector<anari::math::float3> palette;

static anari::math::float3 getColorFromPalette(size_t index)
{
  return palette.at(index % palette.size());
}

static std::vector<anari::math::float3> getColorVectorFromPalette(size_t size) {
  std::vector<anari::math::float3> colors;
  for (size_t i = 0; i < size; ++i) {
    colors.push_back(getColorFromPalette(i));
  }
  return colors;
}
}
