// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace halcyon {

// Inherit from this, add your functionality, and add it to 'createInstance()'.
// Every field (including unknown subtypes) has an ISPC object; a subtype's
// finalize() installs its ISPC sample hook and sets its bounds and step size,
// and a field without a hook is invalid.
struct SpatialField : public Object
{
  SpatialField(HalcyonDeviceGlobalState *d);
  virtual ~SpatialField() = default;
  static SpatialField *createInstance(
      std::string_view subtype, HalcyonDeviceGlobalState *d);

  // Object-space bounds, empty for invalid fields
  box3 bounds() const;
  // Ray marching step size, 0 for invalid fields
  float stepSize() const;
};

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_SPECIALIZATION(
    halcyon::SpatialField *, ANARI_SPATIAL_FIELD);
