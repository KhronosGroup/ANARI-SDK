// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"

namespace helide {

struct Cylinder : public Geometry
{
  Cylinder(HelideGlobalState *s);

  void commit() override;

  float4 getAttributeValue(
      const Attribute &attr, const Ray &ray) const override;

 private:
  void cleanup();

  helium::IntrusivePtr<Array1D> m_index;
  helium::IntrusivePtr<Array1D> m_radius;
  helium::IntrusivePtr<Array1D> m_vertexPosition;
  std::array<helium::IntrusivePtr<Array1D>, 5> m_vertexAttributes;
  float m_globalRadius{0.f};
};

} // namespace helide
