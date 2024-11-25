// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "array/Array1D.h"

namespace helide {

struct Geometry : public Object
{
  Geometry(HelideGlobalState *s);
  ~Geometry() override;

  static Geometry *createInstance(
      std::string_view subtype, HelideGlobalState *s);

  RTCGeometry embreeGeometry() const;

  void commit() override;
  void markCommitted() override;

  virtual float4 getAttributeValue(const Attribute &attr, const Ray &ray) const;
  uint32_t getPrimID(const Ray &ray) const;

 protected:
  RTCGeometry m_embreeGeometry{nullptr};

  UniformAttributeSet m_uniformAttr;
  std::array<helium::IntrusivePtr<Array1D>, 5> m_primitiveAttr;
  helium::IntrusivePtr<Array1D> m_primitiveId;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline uint32_t Geometry::getPrimID(const Ray &ray) const
{
  if (m_primitiveId) {
    return m_primitiveId->elementType() == ANARI_UINT32
        ? *m_primitiveId->valueAt<uint32_t>(ray.primID)
        : uint32_t(*m_primitiveId->valueAt<uint64_t>(ray.primID));
  } else
    return ray.primID;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Geometry *, ANARI_GEOMETRY);
