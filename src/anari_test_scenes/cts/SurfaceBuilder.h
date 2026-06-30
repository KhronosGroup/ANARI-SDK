// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp.hpp"
#include "anari/anari_cpp/ext/linalg.h"

namespace anari {
namespace cts {

anari::Material makeMatteMaterial(
    anari::Device d, anari::math::float3 color);

anari::Surface makeSurface(
    anari::Device d, anari::Geometry geometry, anari::Material material);

} // namespace cts
} // namespace anari
