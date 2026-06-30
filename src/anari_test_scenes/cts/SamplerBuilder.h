// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp.hpp"
// std
#include <string>

namespace anari {
namespace cts {

anari::Sampler makeCheckerboardSampler(
    anari::Device d, bool normalMap = false);

anari::Sampler newImageSampler(anari::Device d,
    const std::string &subtype,
    const std::string &inAttribute,
    bool normalMap = false);

} // namespace cts
} // namespace anari
