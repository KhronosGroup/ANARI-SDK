// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Group.h"

namespace helide {

struct Instance : public Object
{
  Instance(HelideGlobalState *s);
  ~Instance() override;

  void commitParameters() override;
  void finalize() override;
  void markFinalized() override;
  bool isValid() const override;

  uint32_t numTransforms() const;

  const mat4 &xfm(uint32_t i = 0) const;
  const mat4 &invXfm(uint32_t i = 0) const;
  mat3 xfmInvRot(uint32_t i = 0) const;

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
  std::vector<mat4> m_invXfmData;

  uint32_t m_id{~0u};
  helium::ChangeObserverPtr<Array1D> m_idArray;

  UniformAttributeSet m_uniformAttr;
  struct UniformAttributeArrays
  {
    helium::IntrusivePtr<Array1D> attribute0;
    helium::IntrusivePtr<Array1D> attribute1;
    helium::IntrusivePtr<Array1D> attribute2;
    helium::IntrusivePtr<Array1D> attribute3;
    helium::IntrusivePtr<Array1D> color;
  } m_uniformAttrArrays;

  helium::IntrusivePtr<Group> m_group;

  RTCGeometry m_embreeGeometry{nullptr};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline mat3 Instance::xfmInvRot(uint32_t i) const
{
  return linalg::inverse(extractRotation(xfm(i)));
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Instance *, ANARI_INSTANCE);
