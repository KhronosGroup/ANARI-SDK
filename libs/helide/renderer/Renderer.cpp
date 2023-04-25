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
  m_bgColor = getParam<float4>("background", float4(float3(0.f), 1.f));
}

PixelSample Renderer::renderSample(Ray ray, const World &w) const
{
  // Intersect Surfaces //

  RTCIntersectContext context;
  rtcInitIntersectContext(&context);
  rtcIntersect1(w.embreeScene(), &context, (RTCRayHit *)&ray);
  const bool hitGeometry = ray.geomID != RTC_INVALID_GEOMETRY_ID;

  // Intersect Volumes //

  VolumeRay vray;
  vray.org = ray.org;
  vray.dir = ray.dir;
  vray.t.upper = ray.tfar;
  w.intersectVolumes(vray);
  const bool hitVolume = vray.volume != nullptr;

  // Shade //

  if (!hitGeometry && !hitVolume)
    return {m_bgColor, ray.tfar};

  const float3 bgColor(m_bgColor.x, m_bgColor.y, m_bgColor.z);
  const float depth = hitVolume ? std::min(ray.tfar, vray.t.lower) : ray.tfar;

  float3 color(0.f, 0.f, 0.f);
  float opacity = 0.f;

  float3 geometryColor(0.f, 0.f, 0.f);
  float geometryOpacity = 0.f;

  if (hitGeometry) {
    const Instance *inst = w.instances()[ray.instID];
    const Surface *surface = inst->group()->surfaces()[ray.geomID];

    const auto n = linalg::mul(inst->xfmInvRot(), ray.Ng);
    const auto falloff = std::abs(linalg::dot(-ray.dir, linalg::normalize(n)));
    const float3 c = surface->getSurfaceColor(ray);
    const float3 sc = c * falloff;
    geometryColor = 0.8f * sc + 0.2f * c;
    geometryOpacity = 1.f;
  }

  if (hitVolume)
    vray.volume->render(vray, color, opacity);

  accumulateValue(color, geometryColor, opacity);
  accumulateValue(opacity, geometryOpacity, opacity);
  color *= opacity;
  accumulateValue(color, bgColor, opacity);

  return {float4(color, 1.f), depth};
}

Renderer *Renderer::createInstance(
    std::string_view /* subtype */, HelideGlobalState *s)
{
  return new Renderer(s);
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Renderer *);
