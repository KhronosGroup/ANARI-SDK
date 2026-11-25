// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Surface.h"
#include "Surface_ispc.h"

namespace halcyon {

Surface::Surface(HalcyonDeviceGlobalState *s)
    : Object(ANARI_SURFACE, s, ispc::sizeof_Surface())
{}

void Surface::commitParameters()
{
  m_id = getParam<uint32_t>("id", ~0u);
  m_geometry = getParamObject<Geometry>("geometry");
  m_material = getParamObject<Material>("material");
  m_visible = getParam<bool>("visible", true);
  // (an unknown subtype's object isn't a Material, whatever its pointer's
  // type says)
  auto *material =
      dynamic_cast<Material *>(static_cast<helium::BaseObject *>(m_material.ptr));
  m_observedMaterial =
      material && material->validityCanChange() ? material : nullptr;
  m_committed = true;
}

void Surface::finalize()
{
  if (!m_material)
    reportMessage(ANARI_SEVERITY_WARNING, "missing 'material' on ANARISurface");

  if (!m_geometry)
    reportMessage(ANARI_SEVERITY_WARNING, "missing 'geometry' on ANARISurface");

  // shading gives surfaces without a valid material the invalid color; a
  // material whose validity can change is observed, so this can't go stale
  const bool validMaterial = m_material && m_material->isValid();
  // a scene rebuild is due after a commit, or if the material's validity
  // changed what isValid() says (which leaves invalid surfaces out)
  const bool valid = isValid();
  m_sceneChanged = m_committed || valid != m_wasValid;
  m_committed = false;
  m_wasValid = valid;
  ispc::Surface_setMaterial(
      getIE(), validMaterial ? m_material->getIE() : nullptr);
  ispc::Surface_setGeometry(
      getIE(), m_geometry ? m_geometry->getIE() : nullptr);
  ispc::Surface_setId(getIE(), m_id);
}

void Surface::markFinalized()
{
  Object::markFinalized();
  if (m_sceneChanged) {
    deviceState()->objectUpdates.lastBLSReconstructSceneRequest =
        helium::newTimeStamp();
  }
}

bool Surface::isValid() const
{
  const bool validGeometry = m_geometry && m_geometry->isValid();
  if (deviceState()->allowInvalidSurfaceMaterials)
    return validGeometry;
  return validGeometry && m_material && m_material->isValid();
}

bool Surface::isVisible() const
{
  return m_visible;
}

const Geometry *Surface::geometry() const
{
  return m_geometry.ptr;
}

const Material *Surface::material() const
{
  return m_material.ptr;
}

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_DEFINITION(halcyon::Surface *);
