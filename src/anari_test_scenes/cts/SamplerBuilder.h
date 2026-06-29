// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp.hpp"
#include "Export.h"
// std
#include <string>

namespace anari {
namespace cts {

ANARI_CTS_CORE_INTERFACE anari::Sampler makeCheckerboardSampler(
    anari::Device d, bool normalMap = false);

ANARI_CTS_CORE_INTERFACE anari::Sampler newImageSampler(anari::Device d,
    const std::string &subtype,
    const std::string &inAttribute,
    bool normalMap = false);

} // namespace cts
} // namespace anari
