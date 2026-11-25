// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Group.h"
#include "array/Array1D.h"

namespace halcyon {

struct Instance : public Object
{
  Instance(HalcyonDeviceGlobalState *s);
  ~Instance() override;

  void commitParameters() override;
  void finalize() override;
  void markFinalized() override;
  bool isValid() const override;

  uint32_t numTransforms() const;

  const mat4 &xfm(uint32_t i = 0) const;
  const mat4 &invXfm(uint32_t i = 0) const;
  // Normal matrix of transform 'i': the inverse transpose of its linear part
  mat3 normalXfm(uint32_t i = 0) const;

  uint32_t id(uint32_t i = 0) const;

  UniformAttributeSet getUniformAttributes(uint32_t i = 0) const;

  const Group *group() const;
  Group *group();

  RTCGeometry embreeGeometry() const;
  void embreeGeometryUpdate();

 private:
  mat4 m_xfm;
  mat4 m_invXfm;
  helium::ChangeObserverPtr<Array1D> m_xfmArray;
  std::vector<mat4> m_xfmData; // xfm(i), shared w/ ISPC
  std::vector<mat4> m_invXfmData; // invXfm(i), shared w/ ISPC
  std::vector<mat3> m_normalXfmData; // normalXfm(i), shared w/ ISPC
  std::vector<uint32_t> m_ids; // id(i), shared w/ ISPC

  uint32_t m_id{~0u};
  helium::ChangeObserverPtr<Array1D> m_idArray;

  UniformAttributeSet m_uniformAttr;
  // per-transform 'attribute0-3' and 'color' arrays
  std::array<helium::ChangeObserverPtr<Array1D>, NUM_ATTRIBUTES>
      m_uniformAttrArrays;
  // getUniformAttributes(i) of every transform, per set attribute (empty if
  // unset), shared w/ ISPC
  std::array<std::vector<float4>, NUM_ATTRIBUTES> m_attrData;

  helium::IntrusivePtr<Group> m_group;

  RTCGeometry m_embreeGeometry{nullptr};
};

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_SPECIALIZATION(halcyon::Instance *, ANARI_INSTANCE);
