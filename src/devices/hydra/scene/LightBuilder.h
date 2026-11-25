// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Authors a Hydra light sprim from a Light's LightData: a `light` container of
// HdLightTokens params (served to the delegate via GetLightParamValue) plus the
// light's world transform.

#pragma once

#include "light/Light.h"

#include <pxr/imaging/hd/dataSource.h>

namespace anari_hydra {

// Build the light-sprim container. The prim type is `data.type`.
pxr::HdContainerDataSourceHandle BuildLightPrim(const LightData &data);

} // namespace anari_hydra
