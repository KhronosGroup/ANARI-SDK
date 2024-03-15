// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "array/Array1D.h"
#include "array/Array2D.h"
#include "scene/World.h"

namespace helide {

struct PixelSample
{
  float4 color;
  float depth;
  uint32_t primId{~0u};
  uint32_t objId{~0u};
  uint32_t instId{~0u};
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
  GEOMETRY_ATTRIBUTE_COLOR,
  OPACITY_HEATMAP
};

struct Renderer : public Object
{
  Renderer(HelideGlobalState *s);
  ~Renderer() override = default;

  virtual void commit() override;

  PixelSample renderSample(const float2 &screen, Ray ray, const World &w) const;

  static Renderer *createInstance(
      std::string_view subtype, HelideGlobalState *d);

 private:
  float4 shadeRay(const float2 &screen,
      const Ray &ray,
      const VolumeRay &vray,
      const World &w) const;

  float4 m_bgColor{float3(0.f), 1.f};
  float m_ambientRadiance{1.f};
  RenderMode m_mode{RenderMode::DEFAULT};

  helium::IntrusivePtr<Array1D> m_heatmap;
  helium::IntrusivePtr<Array2D> m_bgImage;
};

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Renderer *, ANARI_RENDERER);
