// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp.hpp"
#include "anari_test_scenes_export.h"
// std
#include <string>

namespace anari {
namespace cts {

ANARI_TEST_SCENES_INTERFACE anari::Sampler makeCheckerboardSampler(
    anari::Device d, bool normalMap = false);

ANARI_TEST_SCENES_INTERFACE anari::Sampler newImageSampler(anari::Device d,
    const std::string &subtype,
    const std::string &inAttribute,
    bool normalMap = false);

} // namespace cts
} // namespace anari
