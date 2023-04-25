// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "scene/World.h"

namespace helide {

struct PixelSample
{
  float4 color;
  float depth;
};

struct Renderer : public Object
{
  Renderer(HelideGlobalState *s);
  ~Renderer() override;

  virtual void commit() override;

  PixelSample renderSample(Ray ray, const World &w) const;

  static Renderer *createInstance(
      std::string_view subtype, HelideGlobalState *d);

 protected:
  float4 m_bgColor{float3(0.f), 1.f};
};

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Renderer *, ANARI_RENDERER);
