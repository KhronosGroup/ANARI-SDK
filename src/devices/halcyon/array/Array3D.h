// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/array/Array3D.h"

namespace halcyon {

// Notifies its observers when helium privatizes it; see Array1D.h

using Array3DMemoryDescriptor = helium::Array3DMemoryDescriptor;

struct Array3D : public helium::Array3D
{
  using helium::Array3D::Array3D;

 private:
  void privatize() override
  {
    makePrivatizedCopy(size(0) * size(1) * size(2));
    notifyChangeObservers();
  }
};

} // namespace halcyon

HELIUM_ANARI_TYPEFOR_SPECIALIZATION(halcyon::Array3D *, ANARI_ARRAY3D);
