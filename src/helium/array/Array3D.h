// Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Array.h"

namespace helium {

struct Array3DMemoryDescriptor : public ArrayMemoryDescriptor
{
  uint64_t numItems1{0};
  uint64_t numItems2{0};
  uint64_t numItems3{0};
};

bool isCompact(const Array3DMemoryDescriptor &d);

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
