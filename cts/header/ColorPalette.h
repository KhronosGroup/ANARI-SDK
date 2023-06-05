#pragma once

#include <vector>
#include <glm/gtc/matrix_transform.hpp>

namespace colors {
extern const std::vector<glm::vec3> palette;

static glm::vec3 getColorFromPalette(int index)
{
  return palette.at(index % palette.size());
}

static std::vector<glm::vec3> getColorVectorFromPalette(int size) {
  std::vector<glm::vec3> colors;
  for (int i = 0; i < size; ++i) {
    colors.push_back(getColorFromPalette(i));
  }
  return colors;
}
}
