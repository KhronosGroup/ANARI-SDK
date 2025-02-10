// Copyright 2022-2024 The Khronos Group
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
