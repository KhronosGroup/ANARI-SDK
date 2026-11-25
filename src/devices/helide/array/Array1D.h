// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helide
#include "HelideMath.h"
// helium
#include "helium/array/Array1D.h"
// std
#include <algorithm>

namespace helide {

using Array1DMemoryDescriptor = helium::Array1DMemoryDescriptor;
using Array1D = helium::Array1D;

// A 1D array's elements are [begin, end) of its buffer; helium's data() and
// dataAs() are the start of the whole buffer instead. helide reads 1D arrays
// only through beginAs()/size() and the helpers below, which offset by
// 'begin' themselves. (helium's element accessors valueAt(),
// readAsAttributeValue(), valueAtLinear() and valueAtClosest() also read
// [begin, end) and would give the same results.)

// Element 'i' of 'a', counted from its 'begin', as an attribute value (see
// helium::readAsAttributeValueFlat()); 'i' outside [0, size()) is mapped into
// it by 'wrap'
inline float4 attributeValueAt(
    const Array1D &a, int32_t i, WrapMode wrap = WrapMode::DEFAULT)
{
  const auto idx = calculateWrapIndex(i, a.size(), wrap);
  return helium::readAsAttributeValueFlat(a.begin(), a.elementType(), idx);
}

// attributeValueAt() of 'a', or 'defaultValue' if 'a' is null
inline float4 attributeValueAt(const Array1D *a,
    uint32_t i,
    const float4 &defaultValue = DEFAULT_ATTRIBUTE_VALUE)
{
  return a ? attributeValueAt(*a, int32_t(i)) : defaultValue;
}

// 'a' (elements of type T) sampled at 'in' in [0, 1], interpolating linearly
// between elements [begin, end)
template <typename T>
inline T sampleLinear(const Array1D &a, float in)
{
  const T *data = a.beginAs<T>();
  const auto i = getInterpolant(in, a.size(), false);
  // At in == 1 (and for any 'in' when size is 1) 'upper' is one past the end
  // with a zero weight; clamp it so the read stays in bounds.
  const auto upper = std::min(i.upper, int32_t(a.size()) - 1);
  return linalg::lerp(data[i.lower], data[upper], i.frac);
}

} // namespace helide
