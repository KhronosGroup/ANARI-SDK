// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Renderer.h"

namespace anari {
namespace example_device {

struct AmbientOcclusion : public Renderer
{
  AmbientOcclusion() = default;

  void commit() override;

  RenderedSample renderSample(Ray ray, const World &world) const override;

 private:
  int m_sampleCount{1};
  float m_volumeStepFactor{1.f};
};

} // namespace example_device
} // namespace anari
