// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Surface.h"

namespace anari_hydra {

Surface::Surface(HydraGlobalState *s) : Object(ANARI_SURFACE, s)
{
  m_serial = s->nextSerial++;
}

void Surface::commitParameters()
{
  m_id = getParam<uint32_t>("id", ~0u);
  m_geometry = getParamObject<Geometry>("geometry");
  m_material = getParamObject<Material>("material");
}

void Surface::finalize()
{
  deviceState()->mirrorDirty.mark(this);
}

bool Surface::isValid() const
{
  return m_geometry && m_geometry->isValid();
}

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_DEFINITION(anari_hydra::Surface *);
}
