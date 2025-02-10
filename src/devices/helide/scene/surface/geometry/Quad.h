// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"

namespace helide {

struct Quad : public Geometry
{
  Quad(HelideGlobalState *s);

  void commitParameters() override;
  void finalize() override;

  float4 getAttributeValue(
      const Attribute &attr, const Ray &ray) const override;

 private:
  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  std::array<helium::IntrusivePtr<Array1D>, 5> m_vertexAttributes;
};

} // namespace helide
