// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"

namespace anari_hydra {

// ANARI 'curve' geometry -> Hydra linear basisCurves rprim (SPEC §4.1). v1 maps
// the vertex list to a single linear (polyline) curve; per-vertex width is
// 2*vertex.radius, falling back to the global 'radius'. Segment-index topology
// is a documented follow-up.
struct Curve : public Geometry
{
  Curve(HydraGlobalState *s);
  ~Curve() override = default;

  pxr::TfToken primType() const override;
};

} // namespace anari_hydra
