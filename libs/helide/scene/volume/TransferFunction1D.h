// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Volume.h"
#include "array/Array1D.h"
#include "spatial_field/SpatialField.h"

namespace helide {

struct TransferFunction1D : public Volume
{
  TransferFunction1D(HelideGlobalState *d);
  ~TransferFunction1D() override;

  void commit() override;

  bool isValid() const override;

  box3 bounds() const override;

  void render(const VolumeRay &vray,
      float3 &outputColor,
      float &outputOpacity) override;

 private:
  float3 colorOf(float sample) const;
  float opacityOf(float sample) const;

  const SpatialField *field() const;

  float normalized(float in) const;

  // Data //

  helium::IntrusivePtr<SpatialField> m_field;

  box1 m_valueRange{0.f, 1.f};
  float m_invSize{0.f};
  float m_densityScale{1.f};

  helium::IntrusivePtr<Array1D> m_colorData;
  helium::IntrusivePtr<Array1D> m_opacityData;
};

// Inlined defintions /////////////////////////////////////////////////////////

inline const SpatialField *TransferFunction1D::field() const
{
  return m_field.ptr;
}

inline float3 TransferFunction1D::colorOf(float sample) const
{
  return m_colorData->valueAtLinear<float3>(normalized(sample));
}

inline float TransferFunction1D::opacityOf(float sample) const
{
  return m_opacityData->valueAtLinear<float>(normalized(sample));
}

inline float TransferFunction1D::normalized(float sample) const
{
  return std::clamp(position(sample, m_valueRange), 0.f, 1.f);
}

} // namespace helide
