// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Samples spatial fields set up through scene/volume/spatial_field's exported
// interface for test_spatial_field.cpp.

#include "shading/halcyon_spatialField.isph"

export uniform bool test_isValid(const void *uniform field)
{
  return SpatialField_isValid((const uniform SpatialField *uniform)field);
}

// SpatialField_sampleAt() of the valid 'field' at 'n' packed float3 'points'
export void test_sampleAt(const void *uniform field,
    const uniform float *uniform points,
    uniform float *uniform results,
    uniform int n)
{
  const uniform SpatialField *uniform f =
      (const uniform SpatialField *uniform)field;
  foreach (i = 0 ... n)
    results[i] = SpatialField_sampleAt(f, load_float3(points + 3 * i));
}
