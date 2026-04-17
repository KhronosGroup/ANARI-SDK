// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
// std
#include <string>

namespace helide_gpu {

struct Sampler : public Object
{
  Sampler(HelideGPUDeviceGlobalState *d);
  virtual ~Sampler() = default;
  static Sampler *createInstance(
      std::string_view subtype, HelideGPUDeviceGlobalState *d);

  // 0=unused, 2=image, 3=transform, 4=primitive
  virtual int samplerMode() const;

  int inAttrIndex() const;
  const mat4 &inTransform() const;
  const vec4 &inOffset() const;
  const mat4 &outTransform() const;
  const vec4 &outOffset() const;

 protected:
  void readCommonParameters();
  int resolveAttrIndex(const std::string &name) const;

  std::string m_inAttribute{"attribute0"};
  int m_inAttrIndex{1}; // "attribute0" = slot 1
  mat4 m_inTransform{1.f};
  vec4 m_inOffset{0.f};
  mat4 m_outTransform{1.f};
  vec4 m_outOffset{0.f};
};

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_SPECIALIZATION(helide_gpu::Sampler *, ANARI_SAMPLER);
