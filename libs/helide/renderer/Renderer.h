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

enum class RenderMode
{
  DEFAULT,
  PRIM_ID,
  GEOM_ID,
  INST_ID,
  NG,
  NG_ABS,
  NS,
  NS_ABS,
  RAY_UVW,
  HIT_SURFACE,
  HIT_VOLUME,
  BACKFACE,
  HAS_MATERIAL,
  GEOMETRY_ATTRIBUTE_0,
  GEOMETRY_ATTRIBUTE_1,
  GEOMETRY_ATTRIBUTE_2,
  GEOMETRY_ATTRIBUTE_3,
  GEOMETRY_ATTRIBUTE_COLOR
};

struct Renderer : public Object
{
  Renderer(HelideGlobalState *s);
  ~Renderer() override;

  virtual void commit() override;

  PixelSample renderSample(Ray ray, const World &w) const;

  static Renderer *createInstance(
      std::string_view subtype, HelideGlobalState *d);

 private:
  float3 shadeRay(const Ray &ray, const VolumeRay &vray, const World &w) const;

  float4 m_bgColor{float3(0.f), 1.f};
  float m_ambientRadiance{1.f};
  RenderMode m_mode{RenderMode::DEFAULT};
};

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Renderer *, ANARI_RENDERER);
