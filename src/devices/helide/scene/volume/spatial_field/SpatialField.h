// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace helide {

struct SpatialField : public Object
{
  SpatialField(HelideGlobalState *d);
  virtual ~SpatialField() = default;
  static SpatialField *createInstance(
      std::string_view subtype, HelideGlobalState *d);

  virtual float sampleAt(const float3 &coord) const = 0;

  virtual box3 bounds() const = 0;

  float stepSize() const;

 protected:
  void setStepSize(float size);

 private:
  float m_stepSize{0.f};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline float SpatialField::stepSize() const
{
  return m_stepSize;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(
    helide::SpatialField *, ANARI_SPATIAL_FIELD);
