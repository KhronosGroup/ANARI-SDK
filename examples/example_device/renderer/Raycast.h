// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Renderer.h"

namespace anari {
namespace example_device {

struct Raycast : public Renderer
{
  Raycast() = default;

  RenderedSample renderSample(Ray ray, const World &world) const override;
};

} // namespace example_device
} // namespace anari
