// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstddef>
// pxr
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec4i.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/vtBufferSource.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdAnariTypeHelper
{
 public:
  template <typename T>
  static HdTupleType GetTupleType();

  typedef char PrimvarTypeContainer[sizeof(GfMatrix4d)];
};

#define TYPE_HELPER(T, type)                                                   \
  template <>                                                                  \
  inline HdTupleType HdAnariTypeHelper::GetTupleType<T>()                      \
  {                                                                            \
    return HdTupleType{type, 1};                                               \
  }

TYPE_HELPER(bool, HdTypeBool)
TYPE_HELPER(char, HdTypeInt8)
TYPE_HELPER(short, HdTypeInt16)
TYPE_HELPER(unsigned short, HdTypeUInt16)
TYPE_HELPER(int, HdTypeInt32)
TYPE_HELPER(GfVec2i, HdTypeInt32Vec2)
TYPE_HELPER(GfVec3i, HdTypeInt32Vec3)
TYPE_HELPER(GfVec4i, HdTypeInt32Vec4)
TYPE_HELPER(unsigned int, HdTypeUInt32)
TYPE_HELPER(float, HdTypeFloat)
TYPE_HELPER(GfVec2f, HdTypeFloatVec2)
TYPE_HELPER(GfVec3f, HdTypeFloatVec3)
TYPE_HELPER(GfVec4f, HdTypeFloatVec4)
TYPE_HELPER(double, HdTypeDouble)
TYPE_HELPER(GfVec2d, HdTypeDoubleVec2)
TYPE_HELPER(GfVec3d, HdTypeDoubleVec3)
TYPE_HELPER(GfVec4d, HdTypeDoubleVec4)
TYPE_HELPER(GfMatrix4f, HdTypeFloatMat4)
TYPE_HELPER(GfMatrix4d, HdTypeDoubleMat4)
TYPE_HELPER(GfQuath, HdTypeHalfFloatVec4)
#undef TYPE_HELPER

class HdAnariBufferSampler
{
 public:
  HdAnariBufferSampler(HdVtBufferSource const &buffer) : _buffer(buffer) {}

  bool Sample(int index, void *value, HdTupleType dataType) const;

  template <typename T>
  bool Sample(int index, T *value) const
  {
    return Sample(index,
        static_cast<void *>(value),
        HdAnariTypeHelper::GetTupleType<T>());
  }

 private:
  HdVtBufferSource const &_buffer;
};

class HdAnariPrimvarSampler
{
 public:
  HdAnariPrimvarSampler() = default;
  virtual ~HdAnariPrimvarSampler() = default;

  virtual bool Sample(unsigned int element,
      float u,
      float v,
      void *value,
      HdTupleType dataType) const = 0;

  template <typename T>
  bool Sample(unsigned int element, float u, float v, T *value) const
  {
    return Sample(element,
        u,
        v,
        static_cast<void *>(value),
        HdAnariTypeHelper::GetTupleType<T>());
  }

 protected:
  static bool _Interpolate(void *out,
      void **samples,
      float *weights,
      size_t sampleCount,
      HdTupleType dataType);
};

PXR_NAMESPACE_CLOSE_SCOPE
