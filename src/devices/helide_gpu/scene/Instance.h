// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Group.h"
#include "array/Array1D.h"

namespace helide_gpu {

struct Instance : public Object
{
  Instance(HelideGPUDeviceGlobalState *s);
  ~Instance() override = default;

  void commitParameters() override;
  void finalize() override;

  uint32_t id(size_t i = 0) const;

  size_t numTransforms() const;
  mat4 xfm(size_t i = 0) const;
  bool xfmIsIdentity(size_t i = 0) const;

  const Group *group() const;
  Group *group();

  bool isValid() const override;

 private:
  helium::ChangeObserverPtr<Array1D> m_xfmArray;
  mat4 m_xfm;
  helium::ChangeObserverPtr<Array1D> m_idArray;
  uint32_t m_id{~0u};
  helium::IntrusivePtr<Group> m_group;
};

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_SPECIALIZATION(helide_gpu::Instance *, ANARI_INSTANCE);
