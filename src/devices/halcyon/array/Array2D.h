// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/array/Array2D.h"

namespace halcyon {

// Notifies its observers when helium privatizes it; see Array1D.h

using Array2DMemoryDescriptor = helium::Array2DMemoryDescriptor;

struct Array2D : public helium::Array2D
{
  using helium::Array2D::Array2D;

 private:
  void privatize() override
  {
    makePrivatizedCopy(size(0) * size(1));
    notifyChangeObservers();
  }
};

} // namespace halcyon

HELIUM_ANARI_TYPEFOR_SPECIALIZATION(halcyon::Array2D *, ANARI_ARRAY2D);
