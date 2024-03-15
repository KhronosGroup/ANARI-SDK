// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace hecore {

// Inherit from this, add your functionality, and add it to 'createInstance()'
struct Light : public Object
{
  Light(HeCoreDeviceGlobalState *d);
  ~Light() override = default;
  static Light *createInstance(
      std::string_view subtype, HeCoreDeviceGlobalState *d);
};

} // namespace hecore

HECORE_ANARI_TYPEFOR_SPECIALIZATION(hecore::Light *, ANARI_LIGHT);
