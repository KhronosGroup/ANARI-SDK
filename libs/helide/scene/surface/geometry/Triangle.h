// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"
// anari
#include "anari/backend/utilities/Optional.h"

namespace helide {

struct Triangle : public Geometry
{
  Triangle(HelideGlobalState *s);

  void commit() override;

 private:
  void cleanup();

  helium::IntrusivePtr<Array1D> m_index;
  helium::IntrusivePtr<Array1D> m_vertexPosition;
};

} // namespace helide
