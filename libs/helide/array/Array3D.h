// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "array/Array.h"

namespace helide {

struct Array3DMemoryDescriptor : public ArrayMemoryDescriptor
{
  uint64_t numItems1{0};
  uint64_t numItems2{0};
  uint64_t numItems3{0};
};

bool isCompact(const Array3DMemoryDescriptor &d);

struct Array3D : public Array
{
  Array3D(HelideGlobalState *state, const Array3DMemoryDescriptor &d);

  size_t totalSize() const override;

  size_t size(int dim) const;
  uint3 size() const;

  float4 readAsAttributeValue(int3 i,
      WrapMode wrap1 = WrapMode::DEFAULT,
      WrapMode wrap2 = WrapMode::DEFAULT,
      WrapMode wrap3 = WrapMode::DEFAULT) const;

  void privatize() override;

 private:
  size_t m_size[3] = {0, 0, 0};
};

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Array3D *, ANARI_ARRAY3D);
