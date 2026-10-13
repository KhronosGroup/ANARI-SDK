// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"

namespace anari_hydra {

// ANARI 'quad' geometry -> Hydra mesh rprim (SPEC §4.1). primitive.index is
// UINT32_VEC4 (one quad per entry); when absent, vertices are consumed
// sequentially in independent quads.
struct Quad : public Geometry
{
  Quad(HydraGlobalState *s);
  ~Quad() override = default;

  pxr::TfToken primType() const override;
  int meshVertsPerFace() const override { return 4; }
};

} // namespace anari_hydra
