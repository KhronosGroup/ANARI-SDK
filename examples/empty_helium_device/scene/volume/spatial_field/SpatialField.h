// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace hecore {

// Inherit from this, add your functionality, and add it to 'createInstance()'
struct SpatialField : public Object
{
  SpatialField(HeCoreDeviceGlobalState *d);
  virtual ~SpatialField() = default;
  static SpatialField *createInstance(
      std::string_view subtype, HeCoreDeviceGlobalState *d);
};

} // namespace hecore

HECORE_ANARI_TYPEFOR_SPECIALIZATION(
    hecore::SpatialField *, ANARI_SPATIAL_FIELD);
