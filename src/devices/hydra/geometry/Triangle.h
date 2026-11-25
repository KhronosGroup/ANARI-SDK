// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"

namespace anari_hydra {

// ANARI 'triangle' geometry -> Hydra mesh rprim. Uses the base primitive.index
// (UINT32_VEC3, one triple per triangle); when absent, vertices are consumed
// sequentially as independent triangles.
struct Triangle : public Geometry
{
  Triangle(HydraGlobalState *s);
  ~Triangle() override = default;

  pxr::TfToken primType() const override;
};

} // namespace anari_hydra
