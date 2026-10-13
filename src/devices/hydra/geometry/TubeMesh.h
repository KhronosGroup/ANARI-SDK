// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"
// helium
#include "helium/array/Array1D.h"
// std
#include <string>

namespace anari_hydra {

// Which tube ends to close, from the ANARI `caps` string
// ("none" / "first" / "second" / "both").
struct TubeCaps
{
  bool first{false};
  bool second{false};
};
TubeCaps ParseTubeCaps(const std::string &caps);

// Per-segment radius sources, in priority order: `primitive` (one entry per
// segment, cylinders only, KHR_GEOMETRY_CYLINDER), else `vertex` (per endpoint,
// cones/curves base->apex taper), else the (base, apex) fallback.
struct TubeRadii
{
  helium::Array1D *vertex{nullptr};
  helium::Array1D *primitive{nullptr};
  float base{1.f};
  float apex{1.f};
};

// Commit-time tessellation shared by cone and cylinder: each primitive is a
// tapered tube between two endpoints (p0,r0)->(p1,r1), swept into a mesh with a
// fixed number of radial segments. Cones pass a zero apex radius; cylinders
// pass equal radii. The result is doubleSided; cap fans are still wound outward
// because facing-based shading (Embree) darkens inward faces.
//
// Endpoints come from `position` in consecutive pairs, or via `index`
// (UINT32_VEC2, one segment per entry).
//
// `caps` closes the p0/p1 ends with a triangle fan; a zero-radius end is never
// capped. `vertexCap` (UINT8, indexed by vertex.position index), when present,
// overrides `caps` per endpoint: a nonzero entry caps that endpoint regardless
// of `caps`, and a zero entry leaves it open.
void TessellateTubes(helium::Array1D *position,
    helium::Array1D *index,
    const TubeRadii &radii,
    helium::Array1D *vertexCap,
    TubeCaps caps,
    TessMesh &out);

} // namespace anari_hydra
