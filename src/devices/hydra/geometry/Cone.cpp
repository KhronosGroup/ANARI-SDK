// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Cone.h"
#include "TubeMesh.h"

#include <pxr/imaging/hd/tokens.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace anari_hydra {

Cone::Cone(HydraGlobalState *s) : Geometry(s) {}

void Cone::commitParameters()
{
  Geometry::commitParameters();
  m_caps = getParamString("caps", "none");
}

void Cone::finalize()
{
  Geometry::finalize();
  // Cone tapers base radius -> 0 at the apex when no per-vertex radius is
  // given.
  const TubeRadii radii{
      vertexRadius(), /*primitive=*/nullptr, globalRadius(), /*apex=*/0.f};
  TessellateTubes(vertexPosition(),
      index(),
      radii,
      vertexCap(),
      ParseTubeCaps(m_caps),
      m_mesh);
}

TfToken Cone::primType() const
{
  return HdPrimTypeTokens->mesh;
}

} // namespace anari_hydra
