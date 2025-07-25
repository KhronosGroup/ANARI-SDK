// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Renderer.h"

namespace helide {

// Helper functions ///////////////////////////////////////////////////////////

static RenderMode renderModeFromString(const std::string &name)
{
  if (name == "primitiveId")
    return RenderMode::PRIM_ID;
  else if (name == "objectId")
    return RenderMode::OBJ_ID;
  else if (name == "instanceId")
    return RenderMode::INST_ID;
  else if (name == "embreePrimID")
    return RenderMode::PRIM_INDEX;
  else if (name == "embreeGeomID")
    return RenderMode::GEOM_INDEX;
  else if (name == "embreeInstID")
    return RenderMode::INST_INDEX;
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

void Renderer::commitParameters()
{
  m_bgColor = getParam<float4>("background", float4(float3(0.f), 1.f));
  m_bgImage = getParamObject<Array2D>("background");
  m_ambientRadiance = getParam<float>("ambientRadiance", 1.f);
  m_falloffBlendRatio = getParam<float>("eyeLightBlendRatio", 0.5f);
  m_invVolumeSR = 1.f / getParam<float>("volumeSamplingRate", 1.f);
  m_mode = renderModeFromString(getParamString("mode", "default"));
  m_taskGrainSize.x = getParam<int32_t>("taskGrainSizeWidth", 4);
  m_taskGrainSize.y = getParam<int32_t>("taskGrainSizeHeight", 4);

  bool ignoreAmbientLighting = getParam<bool>("ignoreAmbientLighting", true);
  if (ignoreAmbientLighting)
    m_ambientRadiance = 1.f;
}

PixelSample Renderer::renderSample(
    const float2 &screen, Ray ray, const World &w) const
{
  PixelSample retval;

  // Intersect Surfaces //

  RTCIntersectArguments iargs;
  rtcInitIntersectArguments(&iargs);
  rtcIntersect1(w.embreeScene(), (RTCRayHit *)&ray, &iargs);

  // Intersect Volumes //

  VolumeRay vray;
  vray.org = ray.org;
  vray.dir = ray.dir;
  vray.t.upper = ray.tfar;
  w.intersectVolumes(vray);

  // Shade //

  shadeRay(retval, screen, ray, vray, w);

  return retval;
}

Renderer *Renderer::createInstance(
    std::string_view /* subtype */, HelideGlobalState *s)
{
  return new Renderer(s);
}

void Renderer::shadeRay(PixelSample &retval,
    const float2 &screen,
    const Ray &ray,
    const VolumeRay &vray,
    const World &w) const
{
  const bool hitGeometry = ray.geomID != RTC_INVALID_GEOMETRY_ID;
  const bool hitVolume = vray.volume != nullptr;

  const float4 bgColorOpacity =
      m_bgImage ? backgroundColorFromImage(*m_bgImage, screen) : m_bgColor;

  // Write depth //

  retval.depth = hitVolume ? std::min(ray.tfar, vray.t.lower) : ray.tfar;

  if (!hitGeometry && !hitVolume) {
    retval.color = bgColorOpacity;
    return;
  }

  // Write ids //

  if (hitGeometry || hitVolume) {
    retval.primId =
        hitVolume ? 0 : w.surfaceFromRay(ray)->geometry()->getPrimID(ray);
    retval.objId = hitVolume ? vray.volume->id() : w.surfaceFromRay(ray)->id();
    retval.instId = hitVolume ? w.instanceFromRay(vray)->id(vray.instArrayID)
                              : w.instanceFromRay(ray)->id(ray.instArrayID);
  }

  // Write color //

  const float3 bgColor(bgColorOpacity.x, bgColorOpacity.y, bgColorOpacity.z);

  float3 color(0.f, 0.f, 0.f);
  float opacity = 0.f;

  float3 volumeColor = color;
  float volumeOpacity = 0.f;

  float3 geometryColor(0.f, 0.f, 0.f);

  switch (m_mode) {
  case RenderMode::PRIM_ID:
    geometryColor = makeRandomColor(retval.primId);
    break;
  case RenderMode::OBJ_ID:
    geometryColor = makeRandomColor(retval.objId);
    break;
  case RenderMode::INST_ID:
    geometryColor = makeRandomColor(retval.instId);
    break;
  case RenderMode::PRIM_INDEX:
    geometryColor = hitGeometry ? makeRandomColor(ray.primID) : bgColor;
    break;
  case RenderMode::GEOM_INDEX:
    geometryColor = hitGeometry ? makeRandomColor(ray.geomID) : bgColor;
    break;
  case RenderMode::INST_INDEX:
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
      const float4 sc = surface->getSurfaceColor(
          ray, inst->getUniformAttributes(ray.instArrayID));
      const float so = surface->getSurfaceOpacity(
          ray, inst->getUniformAttributes(ray.instArrayID));
      const float o = surface->adjustedAlpha(std::clamp(sc.w * so, 0.f, 1.f));
      const float3 c = m_heatmap->valueAtLinear<float3>(o);
      const float3 fc = c * falloff;
      geometryColor = (0.8f * fc + 0.2f * c) * m_ambientRadiance;
    }
  } break;
  case RenderMode::DEFAULT:
  default: {
    if (hitGeometry) {
      const Instance *inst = w.instanceFromRay(ray);
      const Surface *surface = w.surfaceFromRay(ray);

      const auto n = linalg::mul(inst->xfmInvRot(ray.instArrayID), ray.Ng);
      const auto falloff =
          std::abs(linalg::dot(-ray.dir, linalg::normalize(n)));
      const float4 c = surface->getSurfaceColor(
          ray, inst->getUniformAttributes(ray.instArrayID));
      const float3 sc = float3(c.x, c.y, c.z) * std::clamp(falloff, 0.f, 1.f);
      geometryColor =
          ((m_falloffBlendRatio * sc)
              + ((1.f - m_falloffBlendRatio) * float3(c.x, c.y, c.z)))
          * m_ambientRadiance;
    }

    if (hitVolume)
      vray.volume->render(vray, m_invVolumeSR, volumeColor, volumeOpacity);

  } break;
  }

  geometryColor = linalg::min(geometryColor, float3(1.f));

  color = linalg::min(volumeColor, float3(1.f));
  opacity = std::clamp(volumeOpacity, 0.f, 1.f);

  if (hitGeometry) {
    accumulateValue(color, geometryColor, opacity);
    accumulateValue(opacity, 1.f, opacity);
  }
  accumulateValue(color, bgColor, opacity);
  accumulateValue(opacity, bgColorOpacity.w, opacity);

  retval.color = float4(color, opacity);
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Renderer *);
