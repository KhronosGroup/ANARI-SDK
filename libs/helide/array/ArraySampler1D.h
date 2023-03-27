// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Array1D.h"
#include "helide_math.h"

namespace helide {

template <typename T>
struct ArraySampler1D
{
  ArraySampler1D(Array1D *data = nullptr);

  T valueAt(float in) const; // 'in' clamped to [0, 1]

 private:
  helium::IntrusivePtr<Array1D> m_array;
};

// Inlined definitions ////////////////////////////////////////////////////////

template <typename T>
inline ArraySampler1D<T>::ArraySampler1D(Array1D *data) : m_array(data)
{}

template <typename T>
inline T ArraySampler1D<T>::valueAt(float in) const
{
  const T *data = m_array->dataAs<T>();

  const uint32_t maxIdx = uint32_t(m_array->size() - 1);
  const float idxf = in * maxIdx;
  const float frac = idxf - std::floor(idxf);
  const uint32_t idx = idxf;

  return linalg::lerp(data[idx], data[std::min(maxIdx, idx + 1)], frac);
}

} // namespace helide