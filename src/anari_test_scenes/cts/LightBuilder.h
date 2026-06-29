// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp.hpp"
#include "anari/anari_cpp/ext/linalg.h"
#include "Export.h"
// std
#include <string>

namespace anari {
namespace cts {

ANARI_CTS_CORE_INTERFACE anari::Light makeDirectionalLight(
    anari::Device d, anari::math::float3 direction, float irradiance = 4.f);

ANARI_CTS_CORE_INTERFACE anari::Light newLight(
    anari::Device d, const std::string &subtype);

} // namespace cts
} // namespace anari
