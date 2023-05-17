// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "array/Array1D.h"

namespace helide {

bool isCompact(const Array1DMemoryDescriptor &d)
{
  return d.byteStride == 0 || d.byteStride == anari::sizeOf(d.elementType);
}

Array1D::Array1D(HelideGlobalState *state, const Array1DMemoryDescriptor &d)
    : Array(ANARI_ARRAY1D, state, d), m_capacity(d.numItems), m_end(d.numItems)
{
  if (!isCompact(d))
    throw std::runtime_error("1D strided arrays not yet supported");
  initManagedMemory();
}

void Array1D::commit()
{
  auto oldBegin = m_begin;
  auto oldEnd = m_end;

  m_begin = getParam<size_t>("begin", 0);
  m_begin = std::clamp(m_begin, size_t(0), m_capacity - 1);
  m_end = getParam<size_t>("end", m_capacity);
  m_end = std::clamp(m_end, size_t(1), m_capacity);

  if (size() == 0) {
    reportMessage(ANARI_SEVERITY_ERROR, "array size must be greater than zero");
    return;
  }

  if (m_begin > m_end) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "array 'begin' is not less than 'end', swapping values");
    std::swap(m_begin, m_end);
  }

  if (m_begin != oldBegin || m_end != oldEnd)
    notifyCommitObservers();
}

size_t Array1D::totalSize() const
{
  return size();
}

size_t Array1D::totalCapacity() const
{
  return m_capacity;
}

void *Array1D::begin() const
{
  auto *p = (unsigned char *)data();
  auto s = anari::sizeOf(elementType());
  return p + (s * m_begin);
}

void *Array1D::end() const
{
  auto *p = (unsigned char *)data();
  auto s = anari::sizeOf(elementType());
  return p + (s * m_end);
}

size_t Array1D::size() const
{
  return m_end - m_begin;
}

void Array1D::privatize()
{
  makePrivatizedCopy(size());
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Array1D *);
