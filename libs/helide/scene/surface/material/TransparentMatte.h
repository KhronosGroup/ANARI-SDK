// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "sampler/Sampler.h"
#include "Material.h"

namespace helide {

struct TransparentMatte : public Material
{
  TransparentMatte(HelideGlobalState *d);
  void commit() override;
};

} // namespace helide
