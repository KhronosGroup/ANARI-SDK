// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Renderer.h"

namespace helide {

Renderer::Renderer(HelideGlobalState *s) : Object(ANARI_RENDERER, s)
{
  s->objectCounts.renderers++;
}

Renderer::~Renderer()
{
  deviceState()->objectCounts.renderers--;
}

void Renderer::commit()
{
  m_bgColor = getParam<float4>("backgroundColor", float4(1.f));
}

float4 Renderer::bgColor() const
{
  return m_bgColor;
}

PixelSample Renderer::renderSample(Ray ray, const World &w) const
{
  RTCIntersectContext context;
  rtcInitIntersectContext(&context);
  rtcIntersect1(w.embreeScene(), &context, (RTCRayHit *)&ray);

  if (ray.geomID == RTC_INVALID_GEOMETRY_ID)
    return {m_bgColor, ray.tfar};

  const Instance *inst = w.instances()[ray.instID];
  const Surface *surface = inst->group()->surfaces()[ray.geomID];

  const auto n = linalg::mul(inst->xfmInvRot(), ray.Ng);
  const auto falloff = std::abs(linalg::dot(-ray.dir, linalg::normalize(n)));
  const float3 c = surface->getSurfaceColor(ray);
  const float3 sc = c * falloff;

  return {float4(0.8f * sc + 0.2f * c, 1.f), ray.tfar};
}

Renderer *Renderer::createInstance(
    std::string_view subtype, HelideGlobalState *s)
{
  return new Renderer(s);
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Renderer *);
