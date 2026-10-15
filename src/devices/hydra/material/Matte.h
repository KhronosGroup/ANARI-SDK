// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Material.h"

namespace anari_hydra {

// ANARI 'matte' -> UsdPreviewSurface with metallic=0, roughness=1:
// color->diffuseColor, opacity.
struct Matte : public Material
{
  Matte(HydraGlobalState *s);
  ~Matte() override = default;

  void commitParameters() override;
};

} // namespace anari_hydra
