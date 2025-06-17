// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "sampler.h"

PXR_NAMESPACE_OPEN_SCOPE

bool HdAnariBufferSampler::Sample(
    int index, void *value, HdTupleType dataType) const
{
  if (_buffer.GetNumElements() <= size_t(index)
      || _buffer.GetTupleType() != dataType)
    return false;

  const size_t elemSize = HdDataSizeOfTupleType(dataType);
  const size_t offset = elemSize * index;
  memcpy(value,
      static_cast<const uint8_t *>(_buffer.GetData()) + offset,
      elemSize);

  return true;
}

template <typename T>
static void _InterpolateImpl(void *out,
    void **samples,
    float *weights,
    size_t sampleCount,
    short numComponents)
{
  // This is an implementation of a general blend of samples:
  // out = sum_j { sample[j] * weights[j] }.
  // Since the vector length comes in as a parameter, and not part
  // of the type, the blend is implemented per component.
  for (short i = 0; i < numComponents; ++i) {
    static_cast<T *>(out)[i] = 0;
    for (size_t j = 0; j < sampleCount; ++j)
      static_cast<T *>(out)[i] += static_cast<T *>(samples[j])[i] * weights[j];
  }
}

bool HdAnariPrimvarSampler::_Interpolate(void *out,
    void **samples,
    float *weights,
    size_t sampleCount,
    HdTupleType dataType)
{
  const short numComponents =
      HdGetComponentCount(dataType.type) * dataType.count;

  const HdType componentType = HdGetComponentType(dataType.type);

  switch (componentType) {
  case HdTypeBool:
    return false;
  case HdTypeInt8:
    _InterpolateImpl<char>(out, samples, weights, sampleCount, numComponents);
  case HdTypeInt16:
    _InterpolateImpl<short>(out, samples, weights, sampleCount, numComponents);
    return true;
  case HdTypeUInt16:
    _InterpolateImpl<unsigned short>(
        out, samples, weights, sampleCount, numComponents);
    return true;
  case HdTypeInt32:
    _InterpolateImpl<int>(out, samples, weights, sampleCount, numComponents);
    return true;
  case HdTypeUInt32:
    _InterpolateImpl<unsigned int>(
        out, samples, weights, sampleCount, numComponents);
    return true;
  case HdTypeFloat:
    _InterpolateImpl<float>(out, samples, weights, sampleCount, numComponents);
    return true;
  case HdTypeDouble:
    _InterpolateImpl<double>(out, samples, weights, sampleCount, numComponents);
    return true;
  default:
    TF_CODING_ERROR("Unsupported type '%s' passed to _Interpolate",
        TfEnum::GetName(componentType).c_str());
    return false;
  }
}

PXR_NAMESPACE_CLOSE_SCOPE
