// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "anari/detail/Helpers.h"
// std
#include <cstdint>
#include <stdexcept>

namespace anari {

size_t sizeOfDataType(ANARIDataType type)
{
  switch (type) {
  case ANARI_DATA_TYPE:
    return sizeof(ANARIDataType);
  case ANARI_LIBRARY:
    return sizeof(ANARILibrary);
  case ANARI_OBJECT:
  case ANARI_CAMERA:
  case ANARI_ARRAY:
  case ANARI_ARRAY1D:
  case ANARI_ARRAY2D:
  case ANARI_ARRAY3D:
  case ANARI_DEVICE:
  case ANARI_GEOMETRY:
  case ANARI_GROUP:
  case ANARI_INSTANCE:
  case ANARI_LIGHT:
  case ANARI_MATERIAL:
  case ANARI_RENDERER:
  case ANARI_SAMPLER:
  case ANARI_SURFACE:
  case ANARI_SPATIAL_FIELD:
  case ANARI_VOLUME:
  case ANARI_WORLD:
    return sizeof(ANARIObject);
  case ANARI_VOID_POINTER:
    return sizeof(void *);
  case ANARI_STRING:
    return sizeof(const char *);
  case ANARI_BOOL:
    return sizeof(bool);
  case ANARI_INT8:
  case ANARI_FIXED8:
    return sizeof(int8_t);
  case ANARI_INT8_VEC2:
  case ANARI_FIXED8_VEC2:
    return 2 * sizeof(int8_t);
  case ANARI_INT8_VEC3:
  case ANARI_FIXED8_VEC3:
    return 3 * sizeof(int8_t);
  case ANARI_INT8_VEC4:
  case ANARI_FIXED8_VEC4:
    return 4 * sizeof(int8_t);
  case ANARI_UINT8:
  case ANARI_UFIXED8:
  case ANARI_UFIXED8_R_SRGB:
    return sizeof(uint8_t);
  case ANARI_UINT8_VEC2:
  case ANARI_UFIXED8_VEC2:
  case ANARI_UFIXED8_RA_SRGB:
    return 2 * sizeof(uint8_t);
  case ANARI_UINT8_VEC3:
  case ANARI_UFIXED8_VEC3:
  case ANARI_UFIXED8_RGB_SRGB:
    return 3 * sizeof(uint8_t);
  case ANARI_UINT8_VEC4:
  case ANARI_UFIXED8_VEC4:
  case ANARI_UFIXED8_RGBA_SRGB:
    return 4 * sizeof(uint8_t);
  case ANARI_INT16:
  case ANARI_FIXED16:
    return sizeof(int16_t);
  case ANARI_INT16_VEC2:
  case ANARI_FIXED16_VEC2:
    return 2 * sizeof(int16_t);
  case ANARI_INT16_VEC3:
  case ANARI_FIXED16_VEC3:
    return 3 * sizeof(int16_t);
  case ANARI_INT16_VEC4:
  case ANARI_FIXED16_VEC4:
    return 4 * sizeof(int16_t);
  case ANARI_UINT16:
  case ANARI_UFIXED16:
    return sizeof(uint16_t);
  case ANARI_UINT16_VEC2:
  case ANARI_UFIXED16_VEC2:
    return 2 * sizeof(uint16_t);
  case ANARI_UINT16_VEC3:
  case ANARI_UFIXED16_VEC3:
    return 3 * sizeof(uint16_t);
  case ANARI_UINT16_VEC4:
  case ANARI_UFIXED16_VEC4:
    return 4 * sizeof(uint16_t);
  case ANARI_INT32:
  case ANARI_FIXED32:
    return sizeof(int32_t);
  case ANARI_INT32_VEC2:
  case ANARI_FIXED32_VEC2:
    return 2 * sizeof(int32_t);
  case ANARI_INT32_VEC3:
  case ANARI_FIXED32_VEC3:
    return 3 * sizeof(int32_t);
  case ANARI_INT32_VEC4:
  case ANARI_FIXED32_VEC4:
    return 4 * sizeof(int32_t);
  case ANARI_INT32_BOX1:
    return 2 * 1 * sizeof(int32_t);
  case ANARI_INT32_BOX2:
    return 2 * 2 * sizeof(int32_t);
  case ANARI_INT32_BOX3:
    return 2 * 3 * sizeof(int32_t);
  case ANARI_INT32_BOX4:
    return 2 * 4 * sizeof(int32_t);
  case ANARI_UINT32:
  case ANARI_UFIXED32:
    return sizeof(uint32_t);
  case ANARI_UINT32_VEC2:
  case ANARI_UFIXED32_VEC2:
    return 2 * sizeof(uint32_t);
  case ANARI_UINT32_VEC3:
  case ANARI_UFIXED32_VEC3:
    return 3 * sizeof(uint32_t);
  case ANARI_UINT32_VEC4:
  case ANARI_UFIXED32_VEC4:
    return 4 * sizeof(uint32_t);
  case ANARI_INT64:
  case ANARI_FIXED64:
    return sizeof(int64_t);
  case ANARI_INT64_VEC2:
  case ANARI_FIXED64_VEC2:
    return 2 * sizeof(int64_t);
  case ANARI_INT64_VEC3:
  case ANARI_FIXED64_VEC3:
    return 3 * sizeof(int64_t);
  case ANARI_INT64_VEC4:
  case ANARI_FIXED64_VEC4:
    return 4 * sizeof(int64_t);
  case ANARI_UINT64:
  case ANARI_UFIXED64:
    return sizeof(uint64_t);
  case ANARI_UINT64_VEC2:
  case ANARI_UFIXED64_VEC2:
    return 2 * sizeof(uint64_t);
  case ANARI_UINT64_VEC3:
  case ANARI_UFIXED64_VEC3:
    return 3 * sizeof(uint64_t);
  case ANARI_UINT64_VEC4:
  case ANARI_UFIXED64_VEC4:
    return 4 * sizeof(uint64_t);
  case ANARI_FLOAT32:
    return sizeof(float);
  case ANARI_FLOAT32_VEC2:
    return 2 * sizeof(float);
  case ANARI_FLOAT32_VEC3:
    return 3 * sizeof(float);
  case ANARI_FLOAT32_VEC4:
    return 4 * sizeof(float);
  case ANARI_FLOAT32_BOX1:
    return 2 * 1 * sizeof(float);
  case ANARI_FLOAT32_BOX2:
    return 2 * 2 * sizeof(float);
  case ANARI_FLOAT32_BOX3:
    return 2 * 3 * sizeof(float);
  case ANARI_FLOAT32_BOX4:
    return 2 * 4 * sizeof(float);
  case ANARI_FLOAT32_MAT2:
    return 3 * 2 * sizeof(float);
  case ANARI_FLOAT32_MAT3:
    return 3 * 3 * sizeof(float);
  case ANARI_FLOAT32_MAT2x3:
    return 4 * 2 * sizeof(float);
  case ANARI_FLOAT32_MAT3x4:
    return 4 * 3 * sizeof(float);
  case ANARI_FLOAT64:
    return sizeof(double);
  case ANARI_UNKNOWN:
  default:
    throw std::runtime_error("sizeOfDataType() called on unsupported type");
  }
}

} // namespace anari
