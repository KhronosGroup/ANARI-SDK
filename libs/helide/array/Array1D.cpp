// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "array/Array1D.h"

namespace helide {

// Helper functions ///////////////////////////////////////////////////////////

template <typename T>
static const T *typedOffset(const void *mem, uint32_t offset)
{
  return ((const T *)mem) + offset;
}

template <typename ELEMENT_T, int NUM_COMPONENTS, bool SRGB = false>
static float4 getAttributeArrayAt_ufixed(void *data, uint32_t offset)
{
  constexpr float m = std::numeric_limits<ELEMENT_T>::max();
  float4 retval(0.f, 0.f, 0.f, 1.f);
  switch (NUM_COMPONENTS) {
  case 4:
    retval.w = toneMap<SRGB>(
        *typedOffset<ELEMENT_T>(data, NUM_COMPONENTS * offset + 3) / m);
  case 3:
    retval.z = toneMap<SRGB>(
        *typedOffset<ELEMENT_T>(data, NUM_COMPONENTS * offset + 2) / m);
  case 2:
    retval.y = toneMap<SRGB>(
        *typedOffset<ELEMENT_T>(data, NUM_COMPONENTS * offset + 1) / m);
  case 1:
    retval.x = toneMap<SRGB>(
        *typedOffset<ELEMENT_T>(data, NUM_COMPONENTS * offset + 0) / m);
  default:
    break;
  }

  return retval;
}

bool isCompact(const Array1DMemoryDescriptor &d)
{
  return d.byteStride == 0 || d.byteStride == anari::sizeOf(d.elementType);
}

// Array1D definitions ////////////////////////////////////////////////////////

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

float4 Array1D::readAsAttributeValue(uint32_t i) const
{
  auto retval = DEFAULT_ATTRIBUTE_VALUE;

  switch (elementType()) {
  case ANARI_FLOAT32:
    std::memcpy(&retval, beginAs<float>() + i, sizeof(float));
    break;
  case ANARI_FLOAT32_VEC2:
    std::memcpy(&retval, beginAs<float2>() + i, sizeof(float2));
    break;
  case ANARI_FLOAT32_VEC3:
    std::memcpy(&retval, beginAs<float3>() + i, sizeof(float3));
    break;
  case ANARI_FLOAT32_VEC4:
    std::memcpy(&retval, beginAs<float4>() + i, sizeof(float4));
    break;
  case ANARI_UFIXED8_R_SRGB:
    retval = getAttributeArrayAt_ufixed<uint8_t, 1, true>(begin(), i);
    break;
  case ANARI_UFIXED8_RA_SRGB:
    retval = getAttributeArrayAt_ufixed<uint8_t, 2, true>(begin(), i);
    break;
  case ANARI_UFIXED8_RGB_SRGB:
    retval = getAttributeArrayAt_ufixed<uint8_t, 3, true>(begin(), i);
    break;
  case ANARI_UFIXED8_RGBA_SRGB:
    retval = getAttributeArrayAt_ufixed<uint8_t, 4, true>(begin(), i);
    break;
  case ANARI_UFIXED8:
    retval = getAttributeArrayAt_ufixed<uint8_t, 1>(begin(), i);
    break;
  case ANARI_UFIXED8_VEC2:
    retval = getAttributeArrayAt_ufixed<uint8_t, 2>(begin(), i);
    break;
  case ANARI_UFIXED8_VEC3:
    retval = getAttributeArrayAt_ufixed<uint8_t, 3>(begin(), i);
    break;
  case ANARI_UFIXED8_VEC4:
    retval = getAttributeArrayAt_ufixed<uint8_t, 4>(begin(), i);
    break;
  case ANARI_UFIXED16:
    retval = getAttributeArrayAt_ufixed<uint16_t, 1>(begin(), i);
    break;
  case ANARI_UFIXED16_VEC2:
    retval = getAttributeArrayAt_ufixed<uint16_t, 2>(begin(), i);
    break;
  case ANARI_UFIXED16_VEC3:
    retval = getAttributeArrayAt_ufixed<uint16_t, 3>(begin(), i);
    break;
  case ANARI_UFIXED16_VEC4:
    retval = getAttributeArrayAt_ufixed<uint16_t, 4>(begin(), i);
    break;
  case ANARI_UFIXED32:
    retval = getAttributeArrayAt_ufixed<uint32_t, 1>(begin(), i);
    break;
  case ANARI_UFIXED32_VEC2:
    retval = getAttributeArrayAt_ufixed<uint32_t, 2>(begin(), i);
    break;
  case ANARI_UFIXED32_VEC3:
    retval = getAttributeArrayAt_ufixed<uint32_t, 3>(begin(), i);
    break;
  case ANARI_UFIXED32_VEC4:
    retval = getAttributeArrayAt_ufixed<uint32_t, 4>(begin(), i);
    break;
  default:
    break;
  }

  return retval;
}

void Array1D::privatize()
{
  makePrivatizedCopy(size());
}

float4 readAttributeValue(const Array1D *arr, uint32_t i)
{
  return arr ? arr->readAsAttributeValue(i) : DEFAULT_ATTRIBUTE_VALUE;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Array1D *);
