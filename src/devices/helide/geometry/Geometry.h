// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "array/Array1D.h"
// std
#include <vector>

namespace helide {

struct Geometry : public Object
{
  Geometry(HelideGlobalState *s);
  ~Geometry() override;

  static Geometry *createInstance(
      std::string_view subtype, HelideGlobalState *s);

  RTCGeometry embreeGeometry() const;

  void commitParameters() override;
  void markFinalized() override;

  virtual float4 getAttributeValue(const Attribute &attr, const Ray &ray) const;
  uint32_t getPrimID(const Ray &ray) const;

 protected:
  // Radii for Embree's float4 (position + radius) vertices: element i of the
  // radius array where it has one, else the global 'radius'
  struct Radii
  {
    const float *values{nullptr};
    size_t count{0};
    float fallback{0.f};

    float operator[](size_t i) const
    {
      return i < count ? values[i] : fallback;
    }
  };

  // Radius array 'array' (parameter 'name', may be null) with 'fallback' for
  // the elements it lacks; warns if it has fewer than 'needed' elements
  Radii readRadii(const Array1D *array,
      const char *name,
      float fallback,
      size_t needed) const;

  // 'primitive.index' array 'index' of 'subtype' geometry as uint32 in 'out'
  // (UINT64 values above UINT32_MAX are truncated, with a warning); false,
  // with a warning, if its elements are neither UINT32 nor UINT64 (callers
  // then leave the geometry empty)
  bool readIndices(const Array1D &index,
      const char *subtype,
      std::vector<uint32_t> &out) const;

  RTCGeometry m_embreeGeometry{nullptr};

  UniformAttributeSet m_uniformAttr;
  std::array<helium::IntrusivePtr<Array1D>, 5> m_primitiveAttr;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline uint32_t Geometry::getPrimID(const Ray &ray) const
{
  return ray.primID;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Geometry *, ANARI_GEOMETRY);
