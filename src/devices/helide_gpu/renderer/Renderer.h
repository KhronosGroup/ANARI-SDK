// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
// std
#include <array>

namespace helide_gpu {

struct Renderer : public Object
{
  Renderer(HelideGPUDeviceGlobalState *s);
  ~Renderer() override = default;

  static Renderer *createInstance(
      std::string_view subtype, HelideGPUDeviceGlobalState *d);

  void commitParameters() override;
  void finalize() override;

  vec4 background() const;
  float ambientRadiance() const;
  float eyeLightBlendRatio() const;

  // SSAO accessors
  bool ssaoEnabled() const;
  float ssaoRadius() const;
  float ssaoBias() const;
  float ssaoIntensity() const;
  int ssaoKernelSize() const;
  bool ssaoBlurEnabled() const;
  const vec4 *ssaoKernel() const;

 private:
  void generateSSAOKernel();

  vec4 m_background{0.f, 0.f, 0.f, 1.f};
  float m_ambientRadiance{1.f};
  float m_eyeLightBlendRatio{0.5f};

  bool m_ignoreAmbientLighting{true};

  // SSAO parameters
  bool m_ssaoEnabled{false};
  float m_ssaoRadius{0.5f};
  float m_ssaoBias{0.025f};
  float m_ssaoIntensity{1.5f};
  int m_ssaoKernelSize{32};
  bool m_ssaoBlurEnabled{true};
  std::array<vec4, 64> m_ssaoKernel{};
};

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_SPECIALIZATION(helide_gpu::Renderer *, ANARI_RENDERER);
