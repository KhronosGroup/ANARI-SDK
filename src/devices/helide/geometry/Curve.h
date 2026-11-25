// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"

namespace helide {

struct Curve : public Geometry
{
  Curve(HelideGlobalState *s);

  void commitParameters() override;
  void finalize() override;

  float4 getAttributeValue(
      const Attribute &attr, const Ray &ray) const override;

 private:
  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::ChangeObserverPtr<Array1D> m_vertexRadius;
  std::array<helium::IntrusivePtr<Array1D>, 5> m_vertexAttributes;
  // 'primitive.index' as uint32, clamped to the last segment (Embree's
  // index buffer, and what attribute interpolation reads)
  std::vector<uint32_t> m_segmentStarts;
  float m_globalRadius{0.f};
};

} // namespace helide
