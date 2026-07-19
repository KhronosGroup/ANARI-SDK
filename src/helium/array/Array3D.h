// Copyright 2023-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Array.h"

namespace helium {

/*
 * Memory descriptor for a 3D array, extending the base descriptor with the
 * element count for each dimension.
 */
struct Array3DMemoryDescriptor : public ArrayMemoryDescriptor
{
  uint64_t numItems1{0};
  uint64_t numItems2{0};
  uint64_t numItems3{0};
};

/*
 * Three-dimensional host array, typically used for volumetric data and 3D
 * textures. Stored as a flat buffer in x-major order; size() returns the
 * dimensions as uint3. readAsAttributeValue() supports independent wrap modes
 * per axis.
 */
struct Array3D : public Array
{
  Array3D(BaseGlobalDeviceState *state, const Array3DMemoryDescriptor &d);

  size_t totalSize() const override;

  size_t size(int dim) const;
  anari::math::uint3 size() const;

  anari::math::float4 readAsAttributeValue(anari::math::int3 i,
      WrapMode wrap1 = WrapMode::DEFAULT,
      WrapMode wrap2 = WrapMode::DEFAULT,
      WrapMode wrap3 = WrapMode::DEFAULT) const;

 private:
  size_t m_size[3] = {0, 0, 0};

 private:
  void privatize() override;
};

} // namespace helium

HELIUM_ANARI_TYPEFOR_SPECIALIZATION(helium::Array3D *, ANARI_ARRAY3D);
