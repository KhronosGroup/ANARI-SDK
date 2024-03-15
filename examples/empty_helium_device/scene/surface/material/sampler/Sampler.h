// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace hecore {

// Inherit from this, add your functionality, and add it to 'createInstance()'
struct Sampler : public Object
{
  Sampler(HeCoreDeviceGlobalState *d);
  virtual ~Sampler() = default;
  static Sampler *createInstance(
      std::string_view subtype, HeCoreDeviceGlobalState *d);
};

} // namespace hecore

HECORE_ANARI_TYPEFOR_SPECIALIZATION(hecore::Sampler *, ANARI_SAMPLER);
