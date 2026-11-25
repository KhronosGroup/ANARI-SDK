// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Cylinder.h"
#include "TubeMesh.h"

#include <pxr/imaging/hd/tokens.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace anari_hydra {

Cylinder::Cylinder(HydraGlobalState *s) : Geometry(s) {}

void Cylinder::commitParameters()
{
  Geometry::commitParameters();
  m_caps = getParamString("caps", "none");
}

void Cylinder::finalize()
{
  Geometry::finalize();
  // Cylinders have no vertex.radius parameter (KHR_GEOMETRY_CYLINDER): radius
  // comes from primitive.radius (per segment, base == apex) or the global
  // 'radius' fallback.
  const TubeRadii radii{
      /*vertex=*/nullptr, primitiveRadius(), globalRadius(), globalRadius()};
  TessellateTubes(vertexPosition(),
      index(),
      radii,
      vertexCap(),
      ParseTubeCaps(m_caps),
      m_mesh);
}

TfToken Cylinder::primType() const
{
  return HdPrimTypeTokens->mesh;
}

} // namespace anari_hydra
