// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../glm_extras.h"
#include "Span.h"

namespace anari {
namespace example_device {

template <typename T>
struct Sampler1D
{
  Sampler1D() = default;
  Sampler1D(Span<T> data);

  T valueAt(float in) const; // 'in' clamped to [0, 1]

 private:
  Span<T> m_data;
};

template <typename T>
Sampler1D<T> make_sampler(T *data, size_t size);

// Inlined definitions ////////////////////////////////////////////////////////

template <typename T>
inline Sampler1D<T>::Sampler1D(Span<T> data)
    : m_data(data)
{}

template <typename T>
inline T Sampler1D<T>::valueAt(float in) const
{
  const int maxIdx = m_data.size() - 1;
  const float idxf = in * maxIdx;
  const float frac = idxf - std::floor(idxf);
  const int idx = idxf;

  return glm::mix(m_data[idx], m_data[std::min(maxIdx, idx + 1)], frac);
}

template <typename T>
inline Sampler1D<T> make_sampler(T *data, size_t size)
{
  return Sampler1D<T>({data, size});
}

} // namespace example_device
} // namespace anari