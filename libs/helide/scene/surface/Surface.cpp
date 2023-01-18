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
  const auto colorAttribute = material()->colorAttribute();
  if (colorAttribute == Attribute::NONE)
    return material()->color();
  else {
    auto v = geometry()->getAttributeValueAt(colorAttribute, ray);
    return float3(v.x, v.y, v.z);
  }
}

float Surface::getSurfaceOpacity(const Ray &ray) const
{
  auto opacityAttribute = material()->opacityAttribute();
  return opacityAttribute == Attribute::NONE
      ? material()->opacity()
      : geometry()->getAttributeValueAt(opacityAttribute, ray).x;
}

void Surface::markCommitted()
{
  Object::markCommitted();
  deviceState()->objectUpdates.lastBLSReconstructSceneRequest =
      helium::newTimeStamp();
}

bool Surface::isValid() const
{
  return m_geometry && m_material && m_geometry->isValid()
      && m_material->isValid();
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Surface *);
