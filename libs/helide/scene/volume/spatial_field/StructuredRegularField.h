// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "SpatialField.h"
#include "array/Array3D.h"

namespace helide {

struct StructuredRegularField : public SpatialField
{
  StructuredRegularField(HelideGlobalState *d);

  void commit() override;

  bool isValid() const override;

  float sampleAt(const float3 &coord) const override;

  box3 bounds() const override;

 private:
  float3 objectToLocal(const float3 &object) const;
  float valueAtVoxel(const uint3 &index) const;

  // Data //

  uint3 m_dims{0u};
  float3 m_origin;
  float3 m_spacing;
  float3 m_invSpacing;
  float3 m_coordUpperBound;

  helium::IntrusivePtr<Array3D> m_dataArray;

  void *m_data{nullptr};
  anari::DataType m_type{ANARI_UNKNOWN};
};

} // namespace helide
