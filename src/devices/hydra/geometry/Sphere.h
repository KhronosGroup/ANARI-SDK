// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"

namespace anari_hydra {

// Indexed spheres become closed triangle meshes with radial vertex normals.
struct Sphere : public Geometry
{
  Sphere(HydraGlobalState *s);
  ~Sphere() override = default;

  void finalize() override;
  pxr::TfToken primType() const override;
  const TessMesh *tessellation() const override
  {
    return &m_mesh;
  }

 private:
  TessMesh m_mesh;
};

} // namespace anari_hydra
