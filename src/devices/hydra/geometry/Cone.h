// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"

namespace anari_hydra {

// ANARI 'cone' geometry -> commit-time-tessellated Hydra mesh (SPEC §4.1). Each
// primitive is a tapered tube between two endpoints; radii come from
// vertex.radius (base->apex), else the global 'radius' base with a zero apex.
// vertex.cap overrides the 'caps' string per endpoint when present; a
// zero-radius apex is never capped regardless.
struct Cone : public Geometry
{
  Cone(HydraGlobalState *s);
  ~Cone() override = default;

  void commitParameters() override;
  void finalize() override;
  pxr::TfToken primType() const override;
  const TessMesh *tessellation() const override { return &m_mesh; }

 private:
  std::string m_caps;
  TessMesh m_mesh;
};

} // namespace anari_hydra
