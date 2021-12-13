// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "StructuredRegularField.h"

namespace anari {
namespace example_device {

void StructuredRegularField::commit()
{
  if (!hasParam("data")) {
    throw std::runtime_error(
        "missing 'data' object on 'structuredRegular' volume!");
  }

  m_dataArray = getParamObject<Array3D>("data");

  m_data = m_dataArray->dataAs<float>();
  m_dims = m_dataArray->size();

  m_origin = getParam<vec3>("origin", vec3(0));
  m_spacing = getParam<vec3>("spacing", vec3(1));

  m_invSpacing = 1.f / m_spacing;
  m_coordUpperBound = vec3(std::nextafter(m_dims.x - 1, 0),
      std::nextafter(m_dims.y - 1, 0),
      std::nextafter(m_dims.z - 1, 0));

  setStepSize(glm::compMin(m_spacing / 2.f));

  box3 volumeBounds(m_origin, m_origin + ((vec3(m_dims) - 1.f) * m_spacing));

  m_box = SpatialFieldBox(volumeBounds);

  buildBVH(&m_box, 1);
}

float StructuredRegularField::sampleAt(const vec3 &coord) const
{
  const vec3 local = objectToLocal(coord);

  if (local.x < 0.f || local.x > m_dims.x - 1.f || local.y < 0.f
      || local.y > m_dims.y - 1.f || local.z < 0.f
      || local.z > m_dims.z - 1.f) {
    return NAN;
  }

  const vec3 clampedLocal = glm::clamp(local, vec3(0), m_coordUpperBound);

  const ivec3 vi0 = ivec3(clampedLocal);
  const ivec3 vi1 = glm::clamp(vi0 + 1, ivec3(0), m_dims - 1);

  const vec3 fracLocal = clampedLocal - vec3(vi0);

  const float voxel_000 = valueAtVoxel(ivec3(vi0.x, vi0.y, vi0.z));
  const float voxel_001 = valueAtVoxel(ivec3(vi1.x, vi0.y, vi0.z));
  const float voxel_010 = valueAtVoxel(ivec3(vi0.x, vi1.y, vi0.z));
  const float voxel_011 = valueAtVoxel(ivec3(vi1.x, vi1.y, vi0.z));
  const float voxel_100 = valueAtVoxel(ivec3(vi0.x, vi0.y, vi1.z));
  const float voxel_101 = valueAtVoxel(ivec3(vi1.x, vi0.y, vi1.z));
  const float voxel_110 = valueAtVoxel(ivec3(vi0.x, vi1.y, vi1.z));
  const float voxel_111 = valueAtVoxel(ivec3(vi1.x, vi1.y, vi1.z));

  const float voxel_00 = voxel_000 + fracLocal.x * (voxel_001 - voxel_000);
  const float voxel_01 = voxel_010 + fracLocal.x * (voxel_011 - voxel_010);
  const float voxel_10 = voxel_100 + fracLocal.x * (voxel_101 - voxel_100);
  const float voxel_11 = voxel_110 + fracLocal.x * (voxel_111 - voxel_110);
  const float voxel_0 = voxel_00 + fracLocal.y * (voxel_01 - voxel_00);
  const float voxel_1 = voxel_10 + fracLocal.y * (voxel_11 - voxel_10);

  return voxel_0 + fracLocal.z * (voxel_1 - voxel_0);
}

vec3 StructuredRegularField::objectToLocal(const vec3 &object) const
{
  return 1.f / (m_spacing) * (object - m_origin);
}

float StructuredRegularField::valueAtVoxel(const ivec3 &index) const
{
  const size_t i = size_t(index.x)
      + m_dims.x * (size_t(index.y) + m_dims.y * size_t(index.z));
  return m_data[i];
}

} // namespace example_device
} // namespace anari
