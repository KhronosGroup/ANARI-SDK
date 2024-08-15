// Copyright 2022-2024 The Khronos Group
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

static float4 backgroundColorFromImage(
    const Array2D &image, const float2 &screen)
{
  const auto interp_x = getInterpolant(screen.x, image.size().x, true);
  const auto interp_y = getInterpolant(screen.y, image.size().y, true);
  const auto v00 = image.readAsAttributeValue({interp_x.lower, interp_y.lower});
  const auto v01 = image.readAsAttributeValue({interp_x.lower, interp_y.upper});
  const auto v10 = image.readAsAttributeValue({interp_x.upper, interp_y.lower});
  const auto v11 = image.readAsAttributeValue({interp_x.upper, interp_y.upper});

  const auto v0 = linalg::lerp(v00, v01, interp_y.frac);
  const auto v1 = linalg::lerp(v10, v11, interp_y.frac);

  return linalg::lerp(v0, v1, interp_x.frac);
}

// Renderer definitions ///////////////////////////////////////////////////////

Renderer::Renderer(HelideGlobalState *s) : Object(ANARI_RENDERER, s)
{
  Array1DMemoryDescriptor md;
  md.elementType = ANARI_FLOAT32_VEC3;
  md.numItems = 4;
  m_heatmap = new Array1D(s, md);
  m_heatmap->refDec(helium::RefType::PUBLIC);

  auto *colors = (float3 *)m_heatmap->map();
  colors[0] = float3(0.f, 0.f, 1.f);
  colors[1] = float3(1.f, 0.f, 0.f);
  colors[2] = float3(1.f, 1.f, 0.f);
  colors[3] = float3(1.f, 1.f, 1.f);
  m_heatmap->unmap();
}

void Renderer::commit()
{
  m_bgColor = getParam<float4>("background", float4(float3(0.f), 1.f));
  m_bgImage = getParamObject<Array2D>("background");
  m_ambientRadiance = getParam<float>("ambientRadiance", 1.f);
  m_mode = renderModeFromString(getParamString("mode", "default"));
}

PixelSample Renderer::renderSample(
    const float2 &screen, Ray ray, const World &w) const
{
  PixelSample retval;

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

  retval.color = shadeRay(screen, ray, vray, w);
  retval.depth = hitVolume ? std::min(ray.tfar, vray.t.lower) : ray.tfar;
  if (hitGeometry || hitVolume) {
    retval.primId = hitVolume ? 0 : ray.primID;
    retval.objId = hitVolume ? vray.volume->id() : w.surfaceFromRay(ray)->id();
    retval.instId = hitVolume ? w.instanceFromRay(vray)->id()
                              : w.instanceFromRay(ray)->id();
  }

  return retval;
}

Renderer *Renderer::createInstance(
    std::string_view /* subtype */, HelideGlobalState *s)
{
  return new Renderer(s);
}

float4 Renderer::shadeRay(const float2 &screen,
    const Ray &ray,
    const VolumeRay &vray,
    const World &w) const
{
  const bool hitGeometry = ray.geomID != RTC_INVALID_GEOMETRY_ID;
  const bool hitVolume = vray.volume != nullptr;

  const float4 bgColorOpacity =
      m_bgImage ? backgroundColorFromImage(*m_bgImage, screen) : m_bgColor;

  if (!hitGeometry && !hitVolume)
    return bgColorOpacity;

  const float3 bgColor(bgColorOpacity.x, bgColorOpacity.y, bgColorOpacity.z);

  float3 color(0.f, 0.f, 0.f);
  float opacity = 0.f;

  float3 volumeColor = color;
  float volumeOpacity = 0.f;

  float3 geometryColor(0.f, 0.f, 0.f);
  float geometryOpacity = hitGeometry ? 1.f : 0.f;

  switch (m_mode) {
  case RenderMode::PRIM_ID:
    geometryColor = hitGeometry ? makeRandomColor(ray.primID) : bgColor;
    break;
  case RenderMode::GEOM_ID:
    geometryColor = hitGeometry ? makeRandomColor(ray.geomID) : bgColor;
    break;
  case RenderMode::INST_ID:
    geometryColor = hitGeometry ? makeRandomColor(ray.instID) : bgColor;
    break;
  case RenderMode::RAY_UVW:
    geometryColor = hitGeometry ? float3(ray.u, ray.v, 1.f) : bgColor;
    break;
  case RenderMode::HIT_SURFACE:
    geometryColor = hitGeometry ? boolColor(hitGeometry) : bgColor;
    break;
  case RenderMode::HIT_VOLUME:
    geometryColor = hitVolume ? boolColor(hitVolume) : bgColor;
    break;
  case RenderMode::BACKFACE:
    geometryColor =
        hitGeometry ? boolColor(linalg::dot(ray.Ng, ray.dir) < 0.f) : bgColor;
    break;
  case RenderMode::NG:
    geometryColor = hitGeometry ? normalize(ray.Ng) : bgColor;
    break;
  case RenderMode::NG_ABS:
    geometryColor = hitGeometry ? normalize(linalg::abs(ray.Ng)) : bgColor;
    break;
  case RenderMode::GEOMETRY_ATTRIBUTE_0:
    geometryColor = hitGeometry
        ? readAttributeValue(Attribute::ATTRIBUTE_0, ray, w)
        : bgColor;
    break;
  case RenderMode::GEOMETRY_ATTRIBUTE_1:
    geometryColor = hitGeometry
        ? readAttributeValue(Attribute::ATTRIBUTE_1, ray, w)
        : bgColor;
    break;
  case RenderMode::GEOMETRY_ATTRIBUTE_2:
    geometryColor = hitGeometry
        ? readAttributeValue(Attribute::ATTRIBUTE_2, ray, w)
        : bgColor;
    break;
  case RenderMode::GEOMETRY_ATTRIBUTE_3:
    geometryColor = hitGeometry
        ? readAttributeValue(Attribute::ATTRIBUTE_3, ray, w)
        : bgColor;
    break;
  case RenderMode::GEOMETRY_ATTRIBUTE_COLOR:
    geometryColor =
        hitGeometry ? readAttributeValue(Attribute::COLOR, ray, w) : bgColor;
    break;
  case RenderMode::OPACITY_HEATMAP: {
    if (hitGeometry) {
      const Instance *inst = w.instanceFromRay(ray);
      const Surface *surface = w.surfaceFromRay(ray);

      const auto n = linalg::mul(inst->xfmInvRot(), ray.Ng);
      const auto falloff =
          std::abs(linalg::dot(-ray.dir, linalg::normalize(n)));
      const float4 sc =
          surface->getSurfaceColor(ray, inst->getUniformAttributes());
      const float so =
          surface->getSurfaceOpacity(ray, inst->getUniformAttributes());
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
      const Instance *inst = w.instanceFromRay(ray);
      const Surface *surface = w.surfaceFromRay(ray);

      const auto n = linalg::mul(inst->xfmInvRot(), ray.Ng);
      const auto falloff =
          std::abs(linalg::dot(-ray.dir, linalg::normalize(n)));
      const float4 c =
          surface->getSurfaceColor(ray, inst->getUniformAttributes());
      const float3 sc = float3(c.x, c.y, c.z) * falloff;
      volumeColor = geometryColor = linalg::min(
          (0.8f * sc + 0.2f * float3(c.x, c.y, c.z)) * m_ambientRadiance,
          float3(1.f));
    }

    if (hitVolume)
      vray.volume->render(vray, volumeColor, volumeOpacity);

  } break;
  }

  geometryColor = linalg::min(geometryColor, float3(1.f));

  color = linalg::min(volumeColor, float3(1.f));
  opacity = volumeOpacity;

  accumulateValue(color, geometryColor * geometryOpacity, opacity);
  accumulateValue(opacity, geometryOpacity, opacity);
  accumulateValue(color, bgColor, opacity);
  accumulateValue(opacity, bgColorOpacity.w, opacity);

  return {color, opacity};
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Renderer *);
