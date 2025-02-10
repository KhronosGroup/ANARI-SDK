// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
// std
#include <limits>

namespace hecore {

// This is just an example struct of what data is associated with each pixel.
struct PixelSample
{
  float4 color{0.f, 1.f, 0.f, 1.f};
  float depth{std::numeric_limits<float>::max()};

  PixelSample(float4 c) : color(c) {}
};

// Inherit from this, add your functionality, and add it to 'createInstance()'
struct Renderer : public Object
{
  Renderer(HeCoreDeviceGlobalState *s);
  ~Renderer() override = default;

  static Renderer *createInstance(
      std::string_view subtype, HeCoreDeviceGlobalState *d);

  void commitParameters() override;

  float4 background() const;

 private:
  float4 m_background{0.f, 0.f, 0.f, 1.f};
};

} // namespace hecore

HECORE_ANARI_TYPEFOR_SPECIALIZATION(hecore::Renderer *, ANARI_RENDERER);
