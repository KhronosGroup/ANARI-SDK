// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Geometry.h"
#include "spatial_field/SpatialField.h"
// std
#include <vector>

namespace anari_hydra {

// Extract the level sets `isovalues` of a dense vertex-centred grid as one
// triangle mesh in the grid's object space (marching tetrahedra over the six
// Kuhn tetrahedra of each cell; watertight inside the domain). Face k's
// primitiveIndices entry is the index of the isovalue it belongs to, and each
// triangle winds so its geometric normal points toward higher field values.
// Vertices are shared between the faces of one level set; no normals are
// generated (README.md "Isosurfaces").
void ExtractIsosurface(const AnariFieldData &grid,
    const std::vector<float> &isovalues,
    TessMesh &out);

// ANARI 'isosurface' geometry (KHR_GEOMETRY_ISOSURFACE) -> a commit-time-
// extracted Hydra mesh (SPEC §4.1). Reads the field's published FieldSnapshot
// grid (the representation volumes also use), so a field commit or in-place
// data edit re-extracts through the field observer. The ANARI primitive is the
// isovalue: primitive.color / attribute0..3 / id are indexed by isovalue.
struct Isosurface : public Geometry
{
  Isosurface(HydraGlobalState *s);
  ~Isosurface() override = default;

  void commitParameters() override;
  void finalize() override;
  // Valid while the extracted surface has at least one triangle: an
  // isosurface with no field, no isovalue, or no crossing renders nothing and
  // contributes no bounds.
  bool isValid() const override;
  pxr::TfToken primType() const override;
  const TessMesh *tessellation() const override { return &m_mesh; }

 private:
  helium::ChangeObserverPtr<SpatialField> m_field;
  helium::ChangeObserverPtr<helium::Array1D> m_isovalueArray;
  float m_isovalue{0.f};
  bool m_hasScalarIsovalue{false};
  TessMesh m_mesh;
};

} // namespace anari_hydra
