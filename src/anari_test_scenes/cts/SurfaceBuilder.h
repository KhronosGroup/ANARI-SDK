// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp.hpp"
#include "anari/anari_cpp/ext/linalg.h"
#include "anari_test_scenes_export.h"

namespace anari {
namespace cts {

ANARI_TEST_SCENES_INTERFACE anari::Material makeMatteMaterial(
    anari::Device d, anari::math::float3 color);

ANARI_TEST_SCENES_INTERFACE anari::Surface makeSurface(
    anari::Device d, anari::Geometry geometry, anari::Material material);

} // namespace cts
} // namespace anari
