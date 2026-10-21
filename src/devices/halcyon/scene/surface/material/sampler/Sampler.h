// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace halcyon {

// Inherit from this, add your functionality, and add it to 'createInstance()'.
// Every sampler (including unknown subtypes) has an ISPC object; a subtype's
// finalize() installs its ISPC sample hook, and a sampler without one is
// invalid and ignored by materials.
struct Sampler : public Object
{
  Sampler(HalcyonDeviceGlobalState *d);
  virtual ~Sampler() = default;
  static Sampler *createInstance(
      std::string_view subtype, HalcyonDeviceGlobalState *d);
};

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_SPECIALIZATION(halcyon::Sampler *, ANARI_SAMPLER);
