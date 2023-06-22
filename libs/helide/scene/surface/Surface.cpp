// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Surface.h"

namespace helide {

Surface::Surface(HelideGlobalState *s) : Object(ANARI_SURFACE, s)
{
  s->objectCounts.surfaces++;
}

Surface::~Surface()
{
  deviceState()->objectCounts.surfaces--;
}

void Surface::commit()
{
  m_geometry = getParamObject<Geometry>("geometry");
  m_material = getParamObject<Material>("material");

  if (!m_material) {
    reportMessage(ANARI_SEVERITY_WARNING, "missing 'material' on ANARISurface");
    return;
  }

  if (!m_geometry) {
    reportMessage(ANARI_SEVERITY_WARNING, "missing 'geometry' on ANARISurface");
    return;
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

float3 Surface::getSurfaceColor(const Ray &ray) const
{
  auto &state = *deviceState();
  auto &imc = state.invalidMaterialColor;

  auto *mat = material();

  if (!mat)
    return float3(imc.x, imc.y, imc.z);

  const auto colorAttribute = mat->colorAttribute();
  const auto *colorSampler = mat->colorSampler();
  if (colorSampler && colorSampler->isValid()) {
    auto v = colorSampler->getSample(*geometry(), ray);
    return float3(v.x, v.y, v.z);
  } else if (colorAttribute == Attribute::NONE)
    return material()->color();
  else {
    auto v = geometry()->getAttributeValue(colorAttribute, ray);
    return float3(v.x, v.y, v.z);
  }
}

void Surface::markCommitted()
{
  Object::markCommitted();
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

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Surface *);
