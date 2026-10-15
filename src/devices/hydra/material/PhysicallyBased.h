// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Material.h"

namespace anari_hydra {

// ANARI 'physicallyBased' -> UsdPreviewSurface. The full parameter table with
// warn-and-drop tail is a follow-up; this slice maps the core
// baseColor/metallic/roughness/opacity block.
struct PhysicallyBased : public Material
{
  PhysicallyBased(HydraGlobalState *s);
  ~PhysicallyBased() override = default;

  void commitParameters() override;
};

} // namespace anari_hydra
