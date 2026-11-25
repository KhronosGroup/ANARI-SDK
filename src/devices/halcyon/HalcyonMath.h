// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/helium_math.h"
// std
#include <array>
#include <optional>

namespace halcyon {

using namespace anari::math;
using namespace helium::math;

// Geometry/instance attributes 'attribute0-3' and 'color', in
// helium::Attribute order (NUM_GEOMETRY_ATTRIBUTES in ISPC)
constexpr int NUM_ATTRIBUTES = 5;
constexpr const char *ATTRIBUTE_NAMES[NUM_ATTRIBUTES] = {
    "attribute0", "attribute1", "attribute2", "attribute3", "color"};

using UniformAttributeSet = std::array<std::optional<float4>, NUM_ATTRIBUTES>;

// Data is handed to ISPC as tightly packed floats and read with the load_*()
// functions in shading/halcyon_math.isph (ISPC's own float3 is 16 bytes), so
// pass e.g. '&v.x' or '&m[0].x', never ISPC-sized copies.
static_assert(sizeof(float2) == 2 * sizeof(float), "float2 must be packed");
static_assert(sizeof(float3) == 3 * sizeof(float), "float3 must be packed");
static_assert(sizeof(float4) == 4 * sizeof(float), "float4 must be packed");
static_assert(sizeof(mat3) == 9 * sizeof(float), "mat3 must be packed");
static_assert(sizeof(mat4) == 16 * sizeof(float), "mat4 must be packed");

} // namespace halcyon
