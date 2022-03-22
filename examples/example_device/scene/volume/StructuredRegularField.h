// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "SpatialField.h"

namespace anari {
namespace example_device {

struct StructuredRegularField
    : public IntersectableObject<SpatialFieldBox, SpatialField>
{
  StructuredRegularField() = default;

  static ANARIParameter g_parameters[];

  void commit() override;

  float sampleAt(const vec3 &coord) const override;

 private:
  vec3 objectToLocal(const vec3 &object) const;
  float valueAtVoxel(const ivec3 &index) const;

  // Data //

  SpatialFieldBox m_box;

  ivec3 m_dims{0};
  vec3 m_origin;
  vec3 m_spacing;
  vec3 m_invSpacing;
  vec3 m_coordUpperBound;

  IntrusivePtr<Array3D> m_dataArray;

  float *m_data{nullptr};
};

} // namespace example_device
} // namespace anari
