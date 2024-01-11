// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "sampler/Sampler.h"

namespace hecore {

// Inherit from this, add your functionality, and add it to 'createInstance()'
struct Material : public Object
{
  Material(HeCoreDeviceGlobalState *s);
  ~Material() override = default;
  static Material *createInstance(
      std::string_view subtype, HeCoreDeviceGlobalState *s);
};

} // namespace hecore

HECORE_ANARI_TYPEFOR_SPECIALIZATION(hecore::Material *, ANARI_MATERIAL);
