// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Material.h"

namespace helide {

struct Matte : public Material
{
  Matte(HelideGlobalState *s);
  void commit() override;
};

} // namespace helide
