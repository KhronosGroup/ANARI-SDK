// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Authors a UsdPreviewSurface-shaped HdMaterialNetworkSchema for an ANARI
// material (SPEC §6.1): a single static topology (one UsdPreviewSurface node,
// one surface terminal) under the universal render context, with per-material
// parameters poked in. Storm shades from this network natively; Embree ignores
// it and falls back to the rprim displayColor (SPEC §6.9). A transform or
// primitive sampler on `normal` adds a Storm-only `glslfx` network on a
// generated variant of the surface shader (SPEC §6.5).

#pragma once

#include "material/Material.h"

#include <pxr/imaging/hd/dataSource.h>

namespace anari_hydra {

// Build the material-sprim container (prim type HdPrimTypeTokens->material).
pxr::HdContainerDataSourceHandle BuildMaterialPrim(Material *material);

} // namespace anari_hydra
