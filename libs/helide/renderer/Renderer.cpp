// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Renderer.h"

namespace helide {

// Helper functions ///////////////////////////////////////////////////////////

static RenderMode renderModeFromString(const std::string &name)
{
  if (name == "primID")
    return RenderMode::PRIM_ID;
  else if (name == "geomID")
    return RenderMode::GEOM_ID;
  else if (name == "instID")
    return RenderMode::INST_ID;
  else if (name == "Ng")
    return RenderMode::NG;
  else if (name == "Ng.abs")
    return RenderMode::NG_ABS;
  else if (name == "uvw")
    return RenderMode::RAY_UVW;
  else if (name == "hitSurface")
    return RenderMode::HIT_SURFACE;
  else if (name == "hitVolume")
    return RenderMode::HIT_VOLUME;
  else if (name == "backface")
    return RenderMode::BACKFACE;
  else if (name == "geometry.attribute0")
    return RenderMode::GEOMETRY_ATTRIBUTE_0;
  else if (name == "geometry.attribute1")
    return RenderMode::GEOMETRY_ATTRIBUTE_1;
  else if (name == "geometry.attribute2")
    return RenderMode::GEOMETRY_ATTRIBUTE_2;
  else if (name == "geometry.attribute3")
    return RenderMode::GEOMETRY_ATTRIBUTE_3;
  else if (name == "geometry.color")
    return RenderMode::GEOMETRY_ATTRIBUTE_COLOR;
  else if (name == "opacityHeatmap")
    return RenderMode::OPACITY_HEATMAP;
  else
    return RenderMode::DEFAULT;
}

static float3 makeRandomColor(uint32_t i)
{
  const uint32_t mx = 13 * 17 * 43;
  const uint32_t my = 11 * 29;
  const uint32_t mz = 7 * 23 * 63;
  const uint32_t g = (i * (3 * 5 * 127) + 12312314);
  return float3((g % mx) * (1.f / (mx - 1)),
      (g % my) * (1.f / (my - 1)),
      (g % mz) * (1.f / (mz - 1)));
}

static float3 boolColor(bool pred)
{
  return pred ? float3(0.f, 1.f, 0.f) : float3(1.f, 0.f, 0.f);
}

static float3 readAttributeValue(Attribute a, const Ray &r, const World &w)
{
  const Instance *inst = w.instances()[r.instID];
  const Surface *surface = inst->group()->surfaces()[r.geomID];
  const Geometry *geom = surface->geometry();
  const auto v = geom->getAttributeValue(a, r);
  return float3(v.x, v.y, v.z);
}

// Renderer definitions ///////////////////////////////////////////////////////

Renderer::Renderer(HelideGlobalState *s) : Object(ANARI_RENDERER, s)
{
  s->objectCounts.renderers++;

  Array1DMemoryDescriptor md;
  md.elementType = ANARI_FLOAT32_VEC3;
  md.numItems = 4;
  m_heatmap = new Array1D(s, md);
  m_heatmap->refDec(helium::RefType::PUBLIC);

  auto *colors = m_heatmap->beginAs<float3>();
  colors[0] = float3(0.f, 0.f, 1.f);
  colors[1] = float3(1.f, 0.f, 0.f);
  colors[2] = float3(1.f, 1.f, 0.f);
  colors[3] = float3(1.f, 1.f, 1.f);
}

Renderer::~Renderer()
{
  deviceState()->objectCounts.renderers--;
}

void Renderer::commit()
{
  m_bgColor = getParam<float4>("background", float4(float3(0.f), 1.f));
  m_ambientRadiance = getParam<float>("ambientRadiance", 1.f);
  m_mode = renderModeFromString(getParamString("mode", "default"));
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

  const float3 color = shadeRay(ray, vray, w);
  const float depth = hitVolume ? std::min(ray.tfar, vray.t.lower) : ray.tfar;
  return {float4(color, 1.f), depth};
}

Renderer *Renderer::createInstance(
    std::string_view /* subtype */, HelideGlobalState *s)
{
  return new Renderer(s);
}

float3 Renderer::shadeRay(
    const Ray &ray, const VolumeRay &vray, const World &w) const
{
  const bool hitGeometry = ray.geomID != RTC_INVALID_GEOMETRY_ID;
  const bool hitVolume = vray.volume != nullptr;

  if (!hitGeometry && !hitVolume)
    return float3(m_bgColor.x, m_bgColor.y, m_bgColor.z);

  const float3 bgColor(m_bgColor.x, m_bgColor.y, m_bgColor.z);

  float3 color(0.f, 0.f, 0.f);
  float opacity = 0.f;

  float3 geometryColor(0.f, 0.f, 0.f);
  float geometryOpacity = hitGeometry ? 1.f : 0.f;

  switch (m_mode) {
  case RenderMode::PRIM_ID:
    color = hitGeometry ? makeRandomColor(ray.primID) : bgColor;
    break;
  case RenderMode::GEOM_ID:
    color = hitGeometry ? makeRandomColor(ray.geomID) : bgColor;
    break;
  case RenderMode::INST_ID:
    color = hitGeometry ? makeRandomColor(ray.instID) : bgColor;
    break;
  case RenderMode::RAY_UVW:
    color = hitGeometry ? float3(ray.u, ray.v, 1.f) : bgColor;
    break;
  case RenderMode::HIT_SURFACE:
    color = hitGeometry ? boolColor(hitGeometry) : bgColor;
    break;
  case RenderMode::HIT_VOLUME:
    color = hitVolume ? boolColor(hitVolume) : bgColor;
    geometryOpacity = 1.f;
    break;
  case RenderMode::BACKFACE:
    color =
        hitGeometry ? boolColor(linalg::dot(ray.Ng, ray.dir) < 0.f) : bgColor;
    break;
  case RenderMode::NG:
    color = hitGeometry ? ray.Ng : bgColor;
    break;
  case RenderMode::NG_ABS:
    color = hitGeometry ? linalg::abs(ray.Ng) : bgColor;
    break;
  case RenderMode::GEOMETRY_ATTRIBUTE_0:
    color = hitGeometry ? readAttributeValue(Attribute::ATTRIBUTE_0, ray, w)
                        : bgColor;
    break;
  case RenderMode::GEOMETRY_ATTRIBUTE_1:
    color = hitGeometry ? readAttributeValue(Attribute::ATTRIBUTE_1, ray, w)
                        : bgColor;
    break;
  case RenderMode::GEOMETRY_ATTRIBUTE_2:
    color = hitGeometry ? readAttributeValue(Attribute::ATTRIBUTE_2, ray, w)
                        : bgColor;
    break;
  case RenderMode::GEOMETRY_ATTRIBUTE_3:
    color = hitGeometry ? readAttributeValue(Attribute::ATTRIBUTE_3, ray, w)
                        : bgColor;
    break;
  case RenderMode::GEOMETRY_ATTRIBUTE_COLOR:
    color =
        hitGeometry ? readAttributeValue(Attribute::COLOR, ray, w) : bgColor;
    break;
  case RenderMode::OPACITY_HEATMAP: {
    if (hitGeometry) {
      const Instance *inst = w.instances()[ray.instID];
      const Surface *surface = inst->group()->surfaces()[ray.geomID];

      const auto n = linalg::mul(inst->xfmInvRot(), ray.Ng);
      const auto falloff =
          std::abs(linalg::dot(-ray.dir, linalg::normalize(n)));
      const float4 sc = surface->getSurfaceColor(ray);
      const float so = surface->getSurfaceOpacity(ray);
      const float o = surface->adjustedAlpha(std::clamp(sc.w * so, 0.f, 1.f));
      const float3 c = m_heatmap->valueAtLinear<float3>(o);
      const float3 fc = c * falloff;
      geometryColor =
          linalg::min((0.8f * fc + 0.2f * c) * m_ambientRadiance, float3(1.f));
    }
  } break;
  case RenderMode::DEFAULT:
  default: {
    if (hitGeometry) {
      const Instance *inst = w.instances()[ray.instID];
      const Surface *surface = inst->group()->surfaces()[ray.geomID];

      const auto n = linalg::mul(inst->xfmInvRot(), ray.Ng);
      const auto falloff =
          std::abs(linalg::dot(-ray.dir, linalg::normalize(n)));
      const float4 c = surface->getSurfaceColor(ray);
      const float3 sc = float3(c.x, c.y, c.z) * falloff;
      geometryColor = linalg::min(
          (0.8f * sc + 0.2f * float3(c.x, c.y, c.z)) * m_ambientRadiance,
          float3(1.f));
    }

    if (hitVolume)
      vray.volume->render(vray, color, opacity);

  } break;
  }

  color = linalg::min(color, float3(1.f));

  accumulateValue(color, geometryColor, opacity);
  accumulateValue(opacity, geometryOpacity, opacity);
  color *= opacity;
  accumulateValue(color, bgColor, opacity);

  return color;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Renderer *);
