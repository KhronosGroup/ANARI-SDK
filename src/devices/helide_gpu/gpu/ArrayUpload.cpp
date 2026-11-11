// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ArrayUpload.h"
#include "HelideGPUSRGB.h"
// helium
#include "helium/array/Array1D.h"
// anari
#include <anari/frontend/type_utility.h>
// std
#include <cstring>
#include <limits>

namespace helide_gpu {

static bool isDirectFloat(ANARIDataType t)
{
  return t == ANARI_FLOAT32 || t == ANARI_FLOAT32_VEC2
      || t == ANARI_FLOAT32_VEC3 || t == ANARI_FLOAT32_VEC4;
}

// The array's first element: a 1D array's elements are [begin, end) of its
// buffer, while data() is the buffer start. 2D/3D arrays have no 'begin'.
static const void *firstElement(const helium::Array *arr)
{
  if (auto *a1d = dynamic_cast<const helium::Array1D *>(arr))
    return a1d->begin();
  return arr->data();
}

const void *ArrayUploadData::bytes() const
{
  return converted ? static_cast<const void *>(convertedValues.data()) : raw;
}

size_t ArrayUploadData::sizeInBytes() const
{
  return converted ? convertedValues.size() * sizeof(float) : rawSizeInBytes;
}

ArrayUploadData arrayUploadData(const helium::Array *arr, bool convertToFloat)
{
  ArrayUploadData retval;

  const ANARIDataType type = arr->elementType();
  const size_t numElements = arr->totalSize();

  if (!convertToFloat || isDirectFloat(type)) {
    retval.raw = firstElement(arr);
    retval.rawSizeInBytes = numElements * anari::sizeOf(type);
    return retval;
  }

  const uint32_t nc = static_cast<uint32_t>(anari::componentsOf(type));
  retval.converted = true;
  auto &converted = retval.convertedValues;
  converted.resize(numElements * nc);

  if (isElementTypeSRGB(type)) {
    const int srgbNC = srgbComponentCount(type);
    const auto *bytes = static_cast<const uint8_t *>(firstElement(arr));
    for (size_t i = 0; i < numElements; ++i) {
      float v[4];
      srgbBytesToLinear(bytes + i * srgbNC, srgbNC, v);
      for (uint32_t c = 0; c < nc; ++c)
        converted[i * nc + c] = v[c];
    }
  } else {
    // readAsAttributeValue() already reads a 1D array from its 'begin'.
    for (size_t i = 0; i < numElements; ++i) {
      auto v = arr->readAsAttributeValue(static_cast<int32_t>(i));
      float tmp[4];
      std::memcpy(tmp, &v, sizeof(tmp));
      for (uint32_t c = 0; c < nc; ++c)
        converted[i * nc + c] = tmp[c];
    }
  }

  return retval;
}

// Normalizes unsigned fixed-point voxels to [0, 1] as readAsAttributeValue()
// does, dividing by the type's maximum as a float.
template <typename T>
static void convertUfixed(const void *src, float *dst, size_t n)
{
  constexpr float m = float(std::numeric_limits<T>::max());
  const auto *s = static_cast<const T *>(src);
  for (size_t i = 0; i < n; ++i)
    dst[i] = s[i] / m;
}

void writeScalarFieldVoxels(const helium::Array *arr, float *dst)
{
  const ANARIDataType type = arr->elementType();
  const size_t numElements = arr->totalSize();
  const void *src = firstElement(arr);

  switch (type) {
  case ANARI_FLOAT32:
    std::memcpy(dst, src, numElements * sizeof(float));
    break;
  case ANARI_UFIXED8:
    convertUfixed<uint8_t>(src, dst, numElements);
    break;
  case ANARI_UFIXED16:
    convertUfixed<uint16_t>(src, dst, numElements);
    break;
  case ANARI_UFIXED32:
    convertUfixed<uint32_t>(src, dst, numElements);
    break;
  default:
    // Other types are rare for fields; read them one element at a time, but
    // without the per-voxel index wrapping of readAsAttributeValue().
    for (size_t i = 0; i < numElements; ++i)
      dst[i] = helium::readAsAttributeValueFlat(src, type, i).x;
    break;
  }
}

} // namespace helide_gpu
