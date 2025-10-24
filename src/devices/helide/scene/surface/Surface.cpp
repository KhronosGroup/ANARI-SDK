// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Surface.h"
#include "../Instance.h"

namespace helide {

Surface::Surface(HelideGlobalState *s) : Object(ANARI_SURFACE, s) {}

void Surface::commitParameters()
{
  m_id = getParam<uint32_t>("id", ~0u);
  m_geometry = getParamObject<Geometry>("geometry");
  m_material = getParamObject<Material>("material");
}

void Surface::finalize()
{
  if (!m_material)
    reportMessage(ANARI_SEVERITY_WARNING, "missing 'material' on ANARISurface");

  if (!m_geometry)
    reportMessage(ANARI_SEVERITY_WARNING, "missing 'geometry' on ANARISurface");
}

void Surface::markFinalized()
{
  Object::markFinalized();
  deviceState()->objectUpdates.lastBLSReconstructSceneRequest =
      helium::newTimeStamp();
}

bool Surface::isValid() const
{
  bool allowInvalidMaterial = deviceState()->allowInvalidSurfaceMaterials;

  if (allowInvalidMaterial) {
    return m_geometry && m_geometry->isValid();
  } else {
    return m_geometry && m_material && m_geometry->isValid()
        && m_material->isValid();
  }
}

const Geometry *Surface::geometry() const
{
  return m_geometry.ptr;
}

const Material *Surface::material() const
{
  return m_material.ptr;
}

float4 Surface::getSurfaceColor(
    const Ray &ray, const UniformAttributeSet &instAttrV) const
{
  const auto &state = *deviceState();
  const auto &imc = state.invalidMaterialColor;

  const auto *mat = material();

  if (!mat)
    return float4(imc.x, imc.y, imc.z, 1.f);

  const auto colorAttribute = mat->colorAttribute();
  const auto *colorSampler = mat->colorSampler();

  if (colorSampler && colorSampler->isValid())
    return colorSampler->getSample(*geometry(), ray, instAttrV);
  else if (colorAttribute == Attribute::WORLD_POSITION)
    return float4(ray.org + ray.dir * ray.tfar, 1.f);
  else if (colorAttribute == Attribute::WORLD_NORMAL)
    return float4(normalize(ray.Ng), 1.f);
  else if (colorAttribute == Attribute::OBJECT_POSITION)
    return float4(
        ray.org + ray.dir * ray.tfar, 1.f); // TODO: transform to object space
  else if (colorAttribute == Attribute::OBJECT_NORMAL)
    return float4(normalize(ray.Ng), 1.f); // TODO: transform to object space
  else if (colorAttribute == Attribute::NONE)
    return mat->color();
  else if (const auto &ia = getUniformAttribute(instAttrV, colorAttribute); ia)
    return *ia;
  else
    return geometry()->getAttributeValue(colorAttribute, ray);
}

float Surface::getSurfaceOpacity(
    const Ray &ray, const UniformAttributeSet &instAttrV) const
{
  auto &state = *deviceState();
  auto &imc = state.invalidMaterialColor;

  auto *mat = material();

  if (!mat)
    return 0.f;

  const auto opacityAttribute = mat->opacityAttribute();
  const auto *opacitySampler = mat->opacitySampler();

  if (opacitySampler && opacitySampler->isValid())
    return opacitySampler->getSample(*geometry(), ray, instAttrV).x;
  else if (opacityAttribute == Attribute::NONE)
    return mat->opacity();
  else if (const auto &ia = getUniformAttribute(instAttrV, opacityAttribute);
      ia)
    return ia->x;
  else
    return geometry()->getAttributeValue(opacityAttribute, ray).x;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Surface *);
