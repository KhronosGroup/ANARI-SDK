// Copyright 2021-2025 The Khronos Group
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

  void commitParameters() override;
  void finalize() override;

  bool isValid() const override;

  box3 bounds() const override;

  void render(const VolumeRay &vray,
      float invSamplingRate,
      float3 &outputColor,
      float &outputOpacity) override;

 private:
  float4 colorOf(float sample) const;
  float opacityOf(float sample) const;

  const SpatialField *field() const;

  float normalized(float in) const;

  // Data //

  helium::ChangeObserverPtr<SpatialField> m_field;

  box1 m_valueRange{0.f, 1.f};
  float m_unitDistance{1.f};
  float4 m_uniformColor{1.f, 1.f, 1.f, 1.f};
  float m_uniformOpacity{1.f};

  helium::ChangeObserverPtr<Array1D> m_colorData;
  helium::ChangeObserverPtr<Array1D> m_opacityData;
};

// Inlined defintions /////////////////////////////////////////////////////////

inline const SpatialField *TransferFunction1D::field() const
{
  return m_field.get();
}

inline float4 TransferFunction1D::colorOf(float sample) const
{
  if (!m_colorData)
    return m_uniformColor;
  else if (m_colorData->elementType() == ANARI_FLOAT32_VEC3)
    return float4(m_colorData->valueAtLinear<float3>(normalized(sample)), 1.f);
  else if (m_colorData->elementType() == ANARI_FLOAT32_VEC4)
    return m_colorData->valueAtLinear<float4>(normalized(sample));
  else
    return float4(0.f); // error, invalid
}

inline float TransferFunction1D::opacityOf(float sample) const
{
  return m_opacityData ? m_opacityData->valueAtLinear<float>(normalized(sample))
                       : m_uniformOpacity;
}

inline float TransferFunction1D::normalized(float sample) const
{
  return std::clamp(position(sample, m_valueRange), 0.f, 1.f);
}

} // namespace helide
