// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp.hpp"
#include "anari/anari_cpp/ext/linalg.h"
// std
#include <array>
#include <cstdint>
#include <functional>

namespace anari {
namespace cts {

using FieldFn = std::function<float(anari::math::float3)>;

float radialDistanceField(anari::math::float3 p);

anari::SpatialField makeStructuredRegularField(
    anari::Device d,
    std::array<uint32_t, 3> dimensions,
    const FieldFn &field = {});

anari::SpatialField newStructuredRegularField(
    anari::Device d,
    std::array<uint32_t, 3> dimensions,
    const FieldFn &field = {});

anari::Volume makeVolume(
    anari::Device d, anari::SpatialField field);

} // namespace cts
} // namespace anari
