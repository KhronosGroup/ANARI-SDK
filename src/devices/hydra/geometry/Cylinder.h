// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"

namespace anari_hydra {

// ANARI 'cylinder' geometry -> commit-time-tessellated Hydra mesh (SPEC §4.1).
// Each primitive is a constant-radius tube between two endpoints; the radius
// comes from primitive.radius (per segment), else the global 'radius'.
// vertex.cap overrides the 'caps' string per endpoint when present.
struct Cylinder : public Geometry
{
  Cylinder(HydraGlobalState *s);
  ~Cylinder() override = default;

  void commitParameters() override;
  void finalize() override;
  pxr::TfToken primType() const override;
  const TessMesh *tessellation() const override { return &m_mesh; }

 private:
  std::string m_caps;
  TessMesh m_mesh;
};

} // namespace anari_hydra
