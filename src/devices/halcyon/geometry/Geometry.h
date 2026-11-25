// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "array/Array1D.h"
// std
#include <memory>

namespace halcyon {

struct Geometry : public Object
{
  Geometry(HalcyonDeviceGlobalState *s);
  ~Geometry() override;
  static Geometry *createInstance(
      std::string_view subtype, HalcyonDeviceGlobalState *s);

  RTCGeometry embreeGeometry() const;

  // Valid once a finalize() has built the Embree geometry; groups leave
  // invalid geometry out of their Embree scenes
  bool isValid() const override;

  // subtypes override this to read their own parameters, calling it first
  void commitParameters() override;
  // Updates 'primitive.id' and attributes, then runs finalizeSubtype()
  void finalize() override final;
  void markFinalized() override;

 protected:
  // The subtype's part of finalize(): installs its ISPC hook (also when it
  // then fails), builds and commits the Embree geometry. True if the geometry
  // is valid.
  virtual bool finalizeSubtype() = 0;

  // Reads 'faceVarying.*' attributes; called from commitParameters() by the
  // subtypes that have them (triangle, quad)
  void commitFaceVaryingAttributes();
  // Reads 'vertex.normal' and 'faceVarying.normal' (FLOAT32_VEC3 or
  // FIXED16_VEC3; others are ignored with a warning); called from
  // commitParameters() by the subtypes that have them (triangle, quad)
  void commitNormals();
  // Reads 'vertex.tangent' and 'faceVarying.tangent' (FLOAT32_VEC3/VEC4 or
  // FIXED16_VEC3/VEC4; others are ignored with a warning); called from
  // commitParameters() by the subtypes that have them (triangle, quad)
  void commitTangents();
  // Builds the tangent frame of every corner of 'numPrimitives' primitives
  // of 'cornersPerPrimitive' (3 or 4) vertices, indexed by 'indices' (packed,
  // nullptr for implicit indexing) into 'numPositions' 'positions', and hands
  // it to ISPC (MeshTangents.h): the authored tangents, else Typhoon's
  // smoothed ones. Called from finalizeSubtype() by triangle and quad, once
  // their ISPC indices and vertices are set.
  void finalizeTangentFrames(uint32_t cornersPerPrimitive,
      size_t numPrimitives,
      const uint32_t *indices,
      const float3 *positions,
      size_t numPositions);
  // Warns about face-varying attribute, normal and tangent arrays with fewer
  // than 'corners' elements, the primitive corners of 'numPrimitives'
  // 'primitives'
  void checkFaceVaryingSizes(
      size_t corners, size_t numPrimitives, const char *primitives) const;
  // Replaces the Embree geometry with a new one of 'type' (a curve whose
  // basis changed); groups attach the new one when they next rebuild, which
  // markFinalized() requests
  void replaceEmbreeGeometry(RTCGeometryType type);

  RTCGeometry m_embreeGeometry{nullptr};

  UniformAttributeSet m_uniformAttr;
  std::array<helium::ChangeObserverPtr<Array1D>, NUM_ATTRIBUTES>
      m_primitiveAttr;
  std::array<helium::ChangeObserverPtr<Array1D>, NUM_ATTRIBUTES> m_vertexAttr;
  std::array<helium::ChangeObserverPtr<Array1D>, NUM_ATTRIBUTES>
      m_faceVaryingAttr;
  helium::ChangeObserverPtr<Array1D> m_primitiveId;
  helium::ChangeObserverPtr<Array1D> m_vertexNormal;
  helium::ChangeObserverPtr<Array1D> m_faceVaryingNormal;
  helium::ChangeObserverPtr<Array1D> m_vertexTangent;
  helium::ChangeObserverPtr<Array1D> m_faceVaryingTangent;

 private:
  void finalizePrimitiveIds();
  void finalizeAttributes();
  void finalizeNormals();

  // set by finalize() from finalizeSubtype()
  bool m_valid{false};
  // m_valid when groups last had to rebuild their Embree scenes for it
  bool m_validInScenes{false};
  // whether groups must rebuild their Embree scenes to attach a new Embree
  // geometry (replaceEmbreeGeometry())
  bool m_embreeGeometryReplaced{false};

  // attribute arrays converted to float4, shared w/ ISPC
  std::array<std::vector<float4>, NUM_ATTRIBUTES> m_primitiveAttrData;
  std::array<std::vector<float4>, NUM_ATTRIBUTES> m_vertexAttrData;
  std::array<std::vector<float4>, NUM_ATTRIBUTES> m_faceVaryingAttrData;
  // normal arrays converted to float4, shared w/ ISPC
  std::vector<float4> m_vertexNormalData;
  std::vector<float4> m_faceVaryingNormalData;
  // the tangent frame per primitive corner as packed float4 (left
  // uninitialized until computed, unlike a vector of float4), shared w/ ISPC
  std::unique_ptr<float[]> m_cornerTangentData;
  std::unique_ptr<float[]> m_cornerBitangentData;
  // 'primitive.id' converted to uint32, shared w/ ISPC
  std::vector<uint32_t> m_primitiveIdData;
};

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_SPECIALIZATION(halcyon::Geometry *, ANARI_GEOMETRY);
