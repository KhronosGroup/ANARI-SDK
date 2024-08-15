// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Group.h"

namespace helide {

struct Instance : public Object
{
  Instance(HelideGlobalState *s);
  ~Instance() override;

  void commit() override;

  uint32_t id() const;

  const mat4 &xfm() const;
  const mat3 &xfmInvRot() const;
  bool xfmIsIdentity() const;

  const UniformAttributeSet &getUniformAttributes() const;

  const Group *group() const;
  Group *group();

  RTCGeometry embreeGeometry() const;
  void embreeGeometryUpdate();

  void markCommitted() override;

  bool isValid() const override;

 private:
  uint32_t m_id{~0u};
  mat4 m_xfm;
  mat3 m_xfmInvRot;
  helium::IntrusivePtr<Group> m_group;
  UniformAttributeSet m_uniformAttr;
  RTCGeometry m_embreeGeometry{nullptr};
};

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Instance *, ANARI_INSTANCE);
