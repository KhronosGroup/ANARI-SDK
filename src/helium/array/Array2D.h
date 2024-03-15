// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Array.h"

namespace helium {

struct Array2DMemoryDescriptor : public ArrayMemoryDescriptor
{
  uint64_t numItems1{0};
  uint64_t numItems2{0};
};

bool isCompact(const Array2DMemoryDescriptor &d);

struct Array2D : public Array
{
  Array2D(BaseGlobalDeviceState *state, const Array2DMemoryDescriptor &d);

  size_t totalSize() const override;

  size_t size(int dim) const;
  anari::math::uint2 size() const;

  anari::math::float4 readAsAttributeValue(anari::math::int2 i,
      WrapMode wrap1 = WrapMode::DEFAULT,
      WrapMode wrap2 = WrapMode::DEFAULT) const;

  void privatize() override;

 private:
  size_t m_size[2] = {0, 0};
};

} // namespace helium

HELIUM_ANARI_TYPEFOR_SPECIALIZATION(helium::Array2D *, ANARI_ARRAY2D);
