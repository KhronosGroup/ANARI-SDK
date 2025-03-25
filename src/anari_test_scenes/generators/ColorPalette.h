#pragma once

#include <vector>
#include "anari/anari_cpp/ext/linalg.h"
#include "anari_test_scenes_export.h"

namespace anari {
namespace scenes {
namespace colors {
extern const std::vector<anari::math::float3> palette;

ANARI_TEST_SCENES_INTERFACE inline anari::math::float3 getColorFromPalette(
    size_t index)
{
  return palette.at(index % palette.size());
}

ANARI_TEST_SCENES_INTERFACE inline std::vector<anari::math::float3>
getColorVectorFromPalette(size_t size)
{
  std::vector<anari::math::float3> colors;
  for (size_t i = 0; i < size; ++i) {
    colors.push_back(getColorFromPalette(i));
  }
  return colors;
}
} // namespace colors
} // namespace scenes
} // namespace anari
