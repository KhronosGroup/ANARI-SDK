// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "scene/Bounds.h"
// helium
#include "helium/array/Array1D.h"
#include "helium/utility/ChangeObserverPtr.h"
// pxr
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <vector>

namespace anari_hydra {

// A commit-time-generated mesh (points + face topology), used by geometry
// subtypes with no direct Hydra rprim (sphere/cone/cylinder -> tessellated
// mesh, isosurface -> extracted mesh, SPEC §4.1). Bulk data here is copied, not
// zero-copy, and lives for the geometry's lifetime so the PrimBuilder can wrap
// it.
struct TessMesh
{
  pxr::VtVec3fArray points;
  // Optional generated normals and source vertices for attribute expansion.
  pxr::VtVec3fArray normals;
  std::vector<uint32_t> vertexIndices;
  pxr::VtIntArray faceVertexCounts;
  pxr::VtIntArray faceVertexIndices;
  // One original ANARI primitive index per generated Hydra face.
  std::vector<uint32_t> primitiveIndices;
};

// Base for ANARI geometry. Each subtype maps to a Hydra rprim type (SPEC §4.1):
// triangle/quad -> mesh, curve -> basisCurves, sphere/cone/cylinder
// -> tessellated mesh, isosurface -> extracted mesh. Subtypes expose committed
// arrays; the
// PrimBuilder reads them to author zero-copy data sources into the mirror.
struct Geometry : public Object
{
  Geometry(HydraGlobalState *s);
  ~Geometry() override = default;

  static Geometry *createInstance(
      std::string_view subtype, HydraGlobalState *s);

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;
  Bounds bounds() const;

  // Hydra rprim type this geometry maps to.
  virtual pxr::TfToken primType() const = 0;

  // Vertices per face for mesh-typed geometry (triangle 3, quad 4). Unused by
  // non-mesh subtypes.
  virtual int meshVertsPerFace() const
  {
    return 3;
  }

  helium::Array1D *vertexPosition() const
  {
    return m_vertexPosition.get();
  }
  // vertex.normal -> the `normals` primvar (vertex interpolation, role normal)
  // on mesh geometry; app-provided only, never generated (SPEC §4.1/§5).
  helium::Array1D *vertexNormal() const
  {
    return m_vertexNormal.get();
  }
  helium::Array1D *vertexColor() const
  {
    return m_vertexColor.get();
  }
  // vertex.radius -> per-vertex radius for spheres/curves/cones (base->apex
  // taper); cylinders take primitive.radius instead (SPEC
  // KHR_GEOMETRY_CYLINDER has no vertex.radius parameter).
  helium::Array1D *vertexRadius() const
  {
    return m_vertexRadius.get();
  }
  // primitive.radius -> per-primitive (per-segment) radius for cylinders.
  helium::Array1D *primitiveRadius() const
  {
    return m_primitiveRadius.get();
  }
  // vertex.cap -> per-endpoint cap override for cone/cylinder tube ends,
  // taking precedence over the subtype's 'caps' string when present.
  helium::Array1D *vertexCap() const
  {
    return m_vertexCap.get();
  }
  // vertex.attribute0 -> the `st` primvar for texture sampling (SPEC §6.3), and
  // the `attribute0` primvar for attribute-bound material inputs (SPEC §6.1).
  helium::Array1D *vertexAttribute0() const
  {
    return m_vertexAttribute0.get();
  }
  // vertex.attribute1..3 -> `attribute1..3` primvars for attribute-bound
  // material inputs (SPEC §6.1).
  helium::Array1D *vertexAttribute1() const
  {
    return m_vertexAttribute1.get();
  }
  helium::Array1D *vertexAttribute2() const
  {
    return m_vertexAttribute2.get();
  }
  helium::Array1D *vertexAttribute3() const
  {
    return m_vertexAttribute3.get();
  }
  // Mesh corner arrays are ordered by face, independently of position indices.
  helium::Array1D *faceVaryingNormal() const
  {
    return m_faceVaryingNormal.get();
  }
  helium::Array1D *faceVaryingColor() const
  {
    return m_faceVaryingColor.get();
  }
  helium::Array1D *faceVaryingAttribute0() const
  {
    return m_faceVaryingAttribute0.get();
  }
  helium::Array1D *faceVaryingAttribute1() const
  {
    return m_faceVaryingAttribute1.get();
  }
  helium::Array1D *faceVaryingAttribute2() const
  {
    return m_faceVaryingAttribute2.get();
  }
  helium::Array1D *faceVaryingAttribute3() const
  {
    return m_faceVaryingAttribute3.get();
  }
  helium::Array1D *primitiveAttribute(int slot) const
  {
    return m_primitiveAttributes[slot].get();
  }
  // Slots 0..3 are attribute0..3; slot 4 is color.
  const helium::float4 *constantAttribute(int slot) const
  {
    return m_hasConstantAttribute[slot] ? &m_constantAttributes[slot] : nullptr;
  }
  bool hasCopiedPrimvars() const;
  std::vector<uint32_t> pickingIds(bool triangulated = false) const;

  // primitive.index; its element type is interpreted by the subtype (triangle
  // UINT32_VEC3, quad UINT32_VEC4, cone/cylinder UINT32_VEC2, curve UINT32).
  helium::Array1D *index() const
  {
    return m_index.get();
  }

  // Global 'radius' fallback (spheres/curves/cones/cylinders), default 1.
  float globalRadius() const
  {
    return m_radius;
  }

  // Non-null for subtypes with no direct rprim: the PrimBuilder authors this
  // generated mesh instead of dispatching on primType(). Recomputed on commit.
  virtual const TessMesh *tessellation() const
  {
    return nullptr;
  }

 protected:
  // ChangeObserverPtr registers this geometry as an observer of its arrays, so
  // an in-place array mutation (anariUnmapArray) re-finalizes the
  // geometry, marking the mirror-dirty set (SPEC §4.5).
  helium::ChangeObserverPtr<helium::Array1D> m_vertexPosition;
  helium::ChangeObserverPtr<helium::Array1D> m_vertexNormal;
  helium::ChangeObserverPtr<helium::Array1D> m_vertexColor;
  helium::ChangeObserverPtr<helium::Array1D> m_vertexRadius;
  helium::ChangeObserverPtr<helium::Array1D> m_primitiveRadius;
  helium::ChangeObserverPtr<helium::Array1D> m_vertexCap;
  helium::ChangeObserverPtr<helium::Array1D> m_vertexAttribute0;
  helium::ChangeObserverPtr<helium::Array1D> m_vertexAttribute1;
  helium::ChangeObserverPtr<helium::Array1D> m_vertexAttribute2;
  helium::ChangeObserverPtr<helium::Array1D> m_vertexAttribute3;
  helium::ChangeObserverPtr<helium::Array1D> m_faceVaryingNormal;
  helium::ChangeObserverPtr<helium::Array1D> m_faceVaryingColor;
  helium::ChangeObserverPtr<helium::Array1D> m_faceVaryingAttribute0;
  helium::ChangeObserverPtr<helium::Array1D> m_faceVaryingAttribute1;
  helium::ChangeObserverPtr<helium::Array1D> m_faceVaryingAttribute2;
  helium::ChangeObserverPtr<helium::Array1D> m_faceVaryingAttribute3;
  helium::ChangeObserverPtr<helium::Array1D> m_index;
  helium::ChangeObserverPtr<helium::Array1D> m_primitiveId;
  helium::ChangeObserverPtr<helium::Array1D> m_primitiveAttributes[5];
  helium::float4 m_constantAttributes[5];
  bool m_hasConstantAttribute[5]{};
  float m_radius{1.f};
};

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_SPECIALIZATION(anari_hydra::Geometry *, ANARI_GEOMETRY);
}
