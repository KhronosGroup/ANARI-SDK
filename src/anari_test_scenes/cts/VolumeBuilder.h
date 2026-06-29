// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp.hpp"
#include "anari/anari_cpp/ext/linalg.h"
#include "anari_test_scenes_export.h"
// std
#include <array>
#include <cstdint>
#include <functional>

namespace anari {
namespace cts {

using FieldFn = std::function<float(anari::math::float3)>;

ANARI_TEST_SCENES_INTERFACE float radialDistanceField(anari::math::float3 p);

ANARI_TEST_SCENES_INTERFACE anari::SpatialField makeStructuredRegularField(
    anari::Device d,
    std::array<uint32_t, 3> dimensions,
    const FieldFn &field = {});

ANARI_TEST_SCENES_INTERFACE anari::SpatialField newStructuredRegularField(
    anari::Device d,
    std::array<uint32_t, 3> dimensions,
    const FieldFn &field = {});

ANARI_TEST_SCENES_INTERFACE anari::Volume makeVolume(
    anari::Device d, anari::SpatialField field);

} // namespace cts
} // namespace anari
