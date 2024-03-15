// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "array/Array2D.h"

namespace helium {

Array2D::Array2D(BaseGlobalDeviceState *state, const Array2DMemoryDescriptor &d)
    : Array(ANARI_ARRAY2D, state, d)
{
  m_size[0] = d.numItems1;
  m_size[1] = d.numItems2;

  initManagedMemory();
}

size_t Array2D::totalSize() const
{
  return size(0) * size(1);
}

size_t Array2D::size(int dim) const
{
  return m_size[dim];
}

uint2 Array2D::size() const
{
  return uint2(uint32_t(size(0)), uint32_t(size(1)));
}

float4 Array2D::readAsAttributeValue(
    int2 i, WrapMode wrap1, WrapMode wrap2) const
{
  const auto i_x = calculateWrapIndex(i.x, size().x, wrap1);
  const auto i_y = calculateWrapIndex(i.y, size().y, wrap2);
  const size_t idx = i_y * size().x + i_x;
  return readAsAttributeValueFlat(data(), elementType(), idx);
}

void Array2D::privatize()
{
  makePrivatizedCopy(size(0) * size(1));
}

} // namespace helium

HELIUM_ANARI_TYPEFOR_DEFINITION(helium::Array2D *);
