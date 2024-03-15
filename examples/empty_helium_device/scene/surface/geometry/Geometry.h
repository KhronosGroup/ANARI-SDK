// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace hecore {

// Inherit from this, add your functionality, and add it to 'createInstance()'
struct Geometry : public Object
{
  Geometry(HeCoreDeviceGlobalState *s);
  ~Geometry() override = default;
  static Geometry *createInstance(
      std::string_view subtype, HeCoreDeviceGlobalState *s);
};

} // namespace hecore

HECORE_ANARI_TYPEFOR_SPECIALIZATION(hecore::Geometry *, ANARI_GEOMETRY);
