// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"

namespace helide {

struct Sphere : public Geometry
{
  Sphere(HelideGlobalState *s);

  void commitParameters() override;
  void finalize() override;

  float4 getAttributeValue(
      const Attribute &attr, const Ray &ray) const override;

 private:
  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::ChangeObserverPtr<Array1D> m_vertexRadius;
  std::array<helium::IntrusivePtr<Array1D>, 5> m_vertexAttributes;
  std::vector<uint32_t> m_attributeIndex;
  float m_globalRadius{0.f};
};

} // namespace helide
