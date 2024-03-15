// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/array/Array1D.h"

namespace hecore {

// You may want to add on additional array functionality by subclassing helium's
// arrays. However, some devices may be able to live with using helium arrays
// directly, so we will just alias them here.

using Array1DMemoryDescriptor = helium::Array1DMemoryDescriptor;
using Array1D = helium::Array1D;

} // namespace hecore
