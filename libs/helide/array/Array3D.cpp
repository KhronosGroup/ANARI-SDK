// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "array/Array3D.h"
// anari
#include "anari/type_utility.h"

namespace helide {

bool isCompact(const Array3DMemoryDescriptor &d)
{
  return (d.byteStride1 == 0 || d.byteStride1 == anari::sizeOf(d.elementType))
      && (d.byteStride2 == 0 || d.byteStride2 == anari::sizeOf(d.elementType))
      && (d.byteStride3 == 0 || d.byteStride3 == anari::sizeOf(d.elementType));
}

Array3D::Array3D(HelideGlobalState *state, const Array3DMemoryDescriptor &d)
    : Array(ANARI_ARRAY3D, state, d)
{
  if (!isCompact(d))
    throw std::runtime_error("3D strided arrays not yet supported");

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

void Array3D::privatize()
{
  makePrivatizedCopy(size(0) * size(1) * size(2));
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Array3D *);
