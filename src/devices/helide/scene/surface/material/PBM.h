// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Material.h"

namespace helide {

struct PBM : public Material
{
  PBM(HelideGlobalState *s);
  void commitParameters() override;
};

} // namespace helide
