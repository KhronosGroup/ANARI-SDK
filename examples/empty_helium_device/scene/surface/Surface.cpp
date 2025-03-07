// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Surface.h"

namespace hecore {

Surface::Surface(HeCoreDeviceGlobalState *s) : Object(ANARI_SURFACE, s) {}

void Surface::commitParameters()
{
  m_id = getParam<uint32_t>("id", ~0u);
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

bool Surface::isValid() const
{
  return m_geometry && m_material && m_geometry->isValid()
      && m_material->isValid();
}

} // namespace hecore

HECORE_ANARI_TYPEFOR_DEFINITION(hecore::Surface *);
