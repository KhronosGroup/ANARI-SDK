// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp.hpp"
#include "anari_test_scenes.h"
#include "anari_test_scenes_export.h"
// std
#include <vector>

namespace anari {
namespace cts {

struct WorldContents
{
  std::vector<anari::Surface> surfaces;
  std::vector<anari::Volume> volumes;
  std::vector<anari::Light> lights;
  std::vector<anari::Instance> instances;
};

ANARI_TEST_SCENES_INTERFACE anari::World assembleWorld(
    anari::Device d, const WorldContents &contents);

ANARI_TEST_SCENES_INTERFACE anari::scenes::Bounds worldBounds(
    anari::Device d, anari::World world);

} // namespace cts
} // namespace anari
