// Copyright 2023-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Array.h"

namespace helium {

/*
 * Memory descriptor for a 1D array, extending the base descriptor with the
 * element count used to determine iteration bounds and totalSize().
 */
struct Array1DMemoryDescriptor : public ArrayMemoryDescriptor
{
  uint64_t numItems{0};
};

/*
 * One-dimensional host array that stores a flat sequence of uniformly typed
 * elements. Supports sub-range iteration (m_begin/m_end are element indices)
 * and provides linear/nearest-neighbor interpolation helpers used by sampler
 * implementations for 1D texture lookups. The readAsAttributeValue() method
 * converts any supported element type to float4 for use as a vertex attribute.
 */
struct Array1D : public Array
{
  Array1D(BaseGlobalDeviceState *state, const Array1DMemoryDescriptor &d);

  void commitParameters() override;
  void finalize() override;

  size_t totalSize() const override;
  size_t totalCapacity() const override;

  const void *begin() const;
  const void *end() const;

  template <typename T>
  const T *beginAs() const;
  template <typename T>
  const T *endAs() const;

  size_t size() const;

 protected:
  size_t m_capacity{0};
  size_t m_begin{0};
  size_t m_end{0};

 private:
  void privatize() override;
};

// Inlined definitions ////////////////////////////////////////////////////////

template <typename T>
inline const T *Array1D::beginAs() const
{
  return dataAs<T>() + m_begin;
}

template <typename T>
inline const T *Array1D::endAs() const
{
  return dataAs<T>() + m_end;
}

} // namespace helium

HELIUM_ANARI_TYPEFOR_SPECIALIZATION(helium::Array1D *, ANARI_ARRAY1D);
