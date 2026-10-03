// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/array/Array.h"
// std
#include <cstddef>
#include <vector>

namespace helide_gpu {

// The bytes GPUBuffer::uploadArray() sends to the GPU for an array: its
// elements as stored, or, when converting to float, the elements converted to
// float components (sRGB types are decoded to linear). For a 1D array these
// are elements [begin, end) of its buffer. Kept free of SDL so it can be
// tested without a GPU device. When not converting, bytes() points into the
// array's own memory, so it is valid only until the array is next changed.
struct ArrayUploadData
{
  const void *bytes() const;
  size_t sizeInBytes() const;

  const void *raw{nullptr};
  size_t rawSizeInBytes{0};
  bool converted{false};
  std::vector<float> convertedValues;
};

ArrayUploadData arrayUploadData(const helium::Array *arr, bool convertToFloat);

} // namespace helide_gpu
