// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "HalcyonMath.h"
// helium
#include "helium/array/Array1D.h"
// std
#include <algorithm>
#include <vector>

namespace halcyon {

// helium copies ("privatizes") a shared array when the app releases its last
// public reference while objects still use it, and then the app may free its
// memory. Geometry passes some arrays to Embree and ISPC in place, so a
// privatize is a change: observers are finalized again at the next flush and
// re-point at the copy. HalcyonDevice::release() holds device work off while
// the copy is made, so a render in progress can't read app memory the app
// frees next. (helium 0.17 notifies here itself, and copies the whole range;
// with it these overrides are redundant, but helium 0.15 and 0.16 need them.)
//
// helium's privatize() overrides are private, so these repeat them.

using Array1DMemoryDescriptor = helium::Array1DMemoryDescriptor;

struct Array1D : public helium::Array1D
{
  using helium::Array1D::Array1D;

 private:
  void privatize() override
  {
    // helium copies size() elements from the start, missing the end of a
    // 'begin'-offset range; copy the whole app buffer instead
    makePrivatizedCopy(totalCapacity());
    notifyChangeObservers();
  }
};

// The first element of 'a': for a 1D array, the one at its 'begin' (helium's
// data() and readAsAttributeValue() start at the buffer's first element)
inline const void *firstElement(const helium::Array &a)
{
  const auto *a1 = dynamic_cast<const helium::Array1D *>(&a);
  return a1 ? a1->begin() : a.data();
}

// Element 'i' of 1D array 'a' (counted from 'begin', clamped to the last one)
// as an attribute value: converted to float4 like helium's
// readAsAttributeValue(), missing components (0, 0, 0, 1); (0, 0, 0, 1) if
// 'a' is empty
inline float4 attributeValueAt(const helium::Array1D &a, size_t i)
{
  if (a.size() == 0)
    return helium::DEFAULT_ATTRIBUTE_VALUE;
  return helium::readAsAttributeValueFlat(
      a.begin(), a.elementType(), std::min(i, a.size() - 1));
}

// Every element of 'a' as an attribute value (see attributeValueAt()) in the
// array's linear order (x fastest for 2D/3D arrays), as the packed float4 data
// ISPC reads (attribute arrays, images); a 1D array's elements are those in
// [begin, end); empty if 'a' is null
inline std::vector<float4> toAttributeValues(const helium::Array *a)
{
  if (!a)
    return {};
  std::vector<float4> values(a->totalSize());
  const void *first = firstElement(*a);
  for (size_t i = 0; i < values.size(); i++)
    values[i] = helium::readAsAttributeValueFlat(first, a->elementType(), i);
  return values;
}

} // namespace halcyon

HELIUM_ANARI_TYPEFOR_SPECIALIZATION(halcyon::Array1D *, ANARI_ARRAY1D);
