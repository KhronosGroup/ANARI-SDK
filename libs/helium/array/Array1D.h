// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Array.h"

namespace helium {

struct Array1DMemoryDescriptor : public ArrayMemoryDescriptor
{
  uint64_t numItems{0};
};

bool isCompact(const Array1DMemoryDescriptor &d);

struct Array1D : public Array
{
  Array1D(BaseGlobalDeviceState *state, const Array1DMemoryDescriptor &d);

  void commit() override;

  size_t totalSize() const override;
  size_t totalCapacity() const override;

  const void *begin() const;
  const void *end() const;

  template <typename T>
  const T *beginAs() const;
  template <typename T>
  const T *endAs() const;

  size_t size() const;

  anari::math::float4 readAsAttributeValue(
      int32_t i, WrapMode wrap = WrapMode::DEFAULT) const;
  template <typename T>
  T valueAtLinear(float in) const; // 'in' must be clamped to [0, 1]
  template <typename T>
  T valueAtClosest(float in) const; // 'in' must be clamped to [0, 1]

  void privatize() override;

 protected:
  size_t m_capacity{0};
  size_t m_begin{0};
  size_t m_end{0};
};

anari::math::float4 readAttributeValue(const Array1D *arr,
    uint32_t i,
    const anari::math::float4 &defaultValue = DEFAULT_ATTRIBUTE_VALUE);

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

template <typename T>
inline T Array1D::valueAtLinear(float in) const
{
  const T *data = dataAs<T>();
  const auto i = getInterpolant(in, size(), false);
  return linalg::lerp(data[i.lower], data[i.upper], i.frac);
}

template <typename T>
inline T Array1D::valueAtClosest(float in) const
{
  const T *data = dataAs<T>();
  const auto i = getInterpolant(in, size(), false);
  return i.frac <= 0.5f ? data[i.lower] : data[i.upper];
}

} // namespace helium

HELIUM_ANARI_TYPEFOR_SPECIALIZATION(helium::Array1D *, ANARI_ARRAY1D);
