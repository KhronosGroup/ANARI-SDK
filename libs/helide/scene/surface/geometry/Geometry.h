// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "array/Array1D.h"
// std
#include <array>

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

 protected:
  RTCGeometry m_embreeGeometry{nullptr};

  std::array<float4, 5> m_uniformAttr;
  std::array<helium::IntrusivePtr<Array1D>, 5> m_primitiveAttr;
};

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Geometry *, ANARI_GEOMETRY);
