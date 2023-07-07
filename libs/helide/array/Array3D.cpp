// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "array/Array3D.h"

namespace helide {

Array3D::Array3D(HelideGlobalState *state, const Array3DMemoryDescriptor &d)
    : Array(ANARI_ARRAY3D, state, d)
{
  m_size[0] = d.numItems1;
  m_size[1] = d.numItems2;
  m_size[2] = d.numItems3;

  initManagedMemory();
}

size_t Array3D::totalSize() const
{
  return size(0) * size(1) * size(2);
}

size_t Array3D::size(int dim) const
{
  return m_size[dim];
}

uint3 Array3D::size() const
{
  return uint3(uint32_t(size(0)), uint32_t(size(1)), uint32_t(size(2)));
}

float4 Array3D::readAsAttributeValue(
    int3 i, WrapMode wrap1, WrapMode wrap2, WrapMode wrap3) const
{
  const auto i_x = calculateWrapIndex(i.x, size().x, wrap1);
  const auto i_y = calculateWrapIndex(i.y, size().y, wrap2);
  const auto i_z = calculateWrapIndex(i.z, size().z, wrap3);
  const size_t idx =
      size_t(i_x) + size().x * (size_t(i_y) + size().y * size_t(i_z));
  return readAsAttributeValueFlat(data(), elementType(), idx);
}

void Array3D::privatize()
{
  makePrivatizedCopy(size(0) * size(1) * size(2));
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Array3D *);
