// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp.hpp"
#include "anari_test_scenes.h"
#include "Export.h"
// std
#include <cstdint>
#include <optional>
#include <string>

namespace anari {
namespace cts {

ANARI_CTS_CORE_INTERFACE anari::Renderer newRenderer(
    anari::Device d, const std::string &subtype = "default");

ANARI_CTS_CORE_INTERFACE anari::scenes::Camera cameraFromBounds(
    const anari::scenes::Bounds &bounds);

struct PerspectiveCameraOptions
{
  std::optional<float> fovy;
  std::optional<float> aspect;
  std::optional<float> near;
  std::optional<float> far;
};

ANARI_CTS_CORE_INTERFACE anari::Camera makePerspectiveCamera(anari::Device d,
    const anari::scenes::Camera &camera,
    const PerspectiveCameraOptions &options = {});

ANARI_CTS_CORE_INTERFACE anari::Frame makeColorFrame(
    anari::Device d, anari::World world, uint32_t width, uint32_t height);

} // namespace cts
} // namespace anari
