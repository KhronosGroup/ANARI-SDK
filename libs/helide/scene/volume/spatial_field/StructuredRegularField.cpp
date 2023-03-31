// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "StructuredRegularField.h"

namespace helide {

StructuredRegularField::StructuredRegularField(HelideGlobalState *d)
    : SpatialField(d)
{}

void StructuredRegularField::commit()
{
  m_dataArray = getParamObject<Array3D>("data");

  if (!m_dataArray) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'data' on 'structuredRegular' field");
    return;
  }

  if (m_dataArray->elementType() != ANARI_FLOAT32) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "only ANARI_FLOAT32 is implemented for "
        "structuredRegular spatial fields");
    m_dataArray = nullptr;
    return;
  }

  m_data = m_dataArray->dataAs<float>();
  m_dims = m_dataArray->size();

  m_origin = getParam<float3>("origin", float3(0.f));
  m_spacing = getParam<float3>("spacing", float3(1.f));

  m_invSpacing = 1.f / m_spacing;
  m_coordUpperBound = float3(std::nextafter(m_dims.x - 1, 0),
      std::nextafter(m_dims.y - 1, 0),
      std::nextafter(m_dims.z - 1, 0));

  setStepSize(linalg::minelem(m_spacing / 2.f));
}

bool StructuredRegularField::isValid() const
{
  return m_dataArray;
}

float StructuredRegularField::sampleAt(const float3 &coord) const
{
  const float3 local = objectToLocal(coord);

  if (local.x < 0.f || local.x > m_dims.x - 1.f || local.y < 0.f
      || local.y > m_dims.y - 1.f || local.z < 0.f
      || local.z > m_dims.z - 1.f) {
    return NAN;
  }

  const float3 clampedLocal =
      linalg::clamp(local, float3(0.f), m_coordUpperBound);

  const uint3 vi0 = uint3(clampedLocal);
  const uint3 vi1 = linalg::clamp(vi0 + 1, uint3(0u), m_dims - 1);

  const float3 fracLocal = clampedLocal - float3(vi0);

  const float voxel_000 = valueAtVoxel(uint3(vi0.x, vi0.y, vi0.z));
  const float voxel_001 = valueAtVoxel(uint3(vi1.x, vi0.y, vi0.z));
  const float voxel_010 = valueAtVoxel(uint3(vi0.x, vi1.y, vi0.z));
  const float voxel_011 = valueAtVoxel(uint3(vi1.x, vi1.y, vi0.z));
  const float voxel_100 = valueAtVoxel(uint3(vi0.x, vi0.y, vi1.z));
  const float voxel_101 = valueAtVoxel(uint3(vi1.x, vi0.y, vi1.z));
  const float voxel_110 = valueAtVoxel(uint3(vi0.x, vi1.y, vi1.z));
  const float voxel_111 = valueAtVoxel(uint3(vi1.x, vi1.y, vi1.z));

  const float voxel_00 = voxel_000 + fracLocal.x * (voxel_001 - voxel_000);
  const float voxel_01 = voxel_010 + fracLocal.x * (voxel_011 - voxel_010);
  const float voxel_10 = voxel_100 + fracLocal.x * (voxel_101 - voxel_100);
  const float voxel_11 = voxel_110 + fracLocal.x * (voxel_111 - voxel_110);
  const float voxel_0 = voxel_00 + fracLocal.y * (voxel_01 - voxel_00);
  const float voxel_1 = voxel_10 + fracLocal.y * (voxel_11 - voxel_10);

  return voxel_0 + fracLocal.z * (voxel_1 - voxel_0);
}

box3 StructuredRegularField::bounds() const
{
  return box3(m_origin, m_origin + ((float3(m_dims) - 1.f) * m_spacing));
}

float3 StructuredRegularField::objectToLocal(const float3 &object) const
{
  return 1.f / (m_spacing) * (object - m_origin);
}

float StructuredRegularField::valueAtVoxel(const uint3 &index) const
{
  const size_t i = size_t(index.x)
      + m_dims.x * (size_t(index.y) + m_dims.y * size_t(index.z));
  return m_data[i];
}

} // namespace helide
