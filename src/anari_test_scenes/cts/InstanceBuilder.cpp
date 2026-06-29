// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "InstanceBuilder.h"
// std
#include <cstring>

namespace anari {
namespace cts {

anari::Instance makeInstance(anari::Device d,
    const std::vector<anari::Surface> &surfaces,
    math::mat4 transform)
{
  auto group = anari::newObject<anari::Group>(d);
  if (!surfaces.empty()) {
    anari::setAndReleaseParameter(d,
        group,
        "surface",
        anari::newArray1D(d, surfaces.data(), surfaces.size()));
  }
  anari::commitParameters(d, group);

  auto instance = anari::newObject<anari::Instance>(d, "transform");
  float matrix[16];
  std::memcpy(matrix, &transform, sizeof(matrix));
  anari::setParameter(d, instance, "transform", matrix);
  anari::setAndReleaseParameter(d, instance, "group", group);
  anari::commitParameters(d, instance);
  return instance;
}

} // namespace cts
} // namespace anari
