// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp.hpp"
#include "anari/anari_cpp/ext/linalg.h"
// std
#include <vector>

namespace anari {
namespace cts {

anari::Instance makeInstance(anari::Device d,
    const std::vector<anari::Surface> &surfaces,
    anari::math::mat4 transform = anari::math::identity);

} // namespace cts
} // namespace anari
