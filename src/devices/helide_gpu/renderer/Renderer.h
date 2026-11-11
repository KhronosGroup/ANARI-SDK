// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

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
  // The renderer's light, split between a uniform ambient fill (radiance
  // (1 - eyeLightBlendRatio) * ambientRadiance * ambientColor) and a headlight
  // (eyeLightBlendRatio * ambientRadiance) that only lights a world without
  // lights. With 'ignoreAmbientLighting', ambientRadiance is 1 and
  // ambientColor white.
  float ambientRadiance() const;
  vec3 ambientColor() const;
  float eyeLightBlendRatio() const;

  // Screen-space ambient occlusion, as in halcyon's preview renderer
  bool ambientOcclusion() const;
  // 'ambientOcclusionRadius', or if that is 0, a fraction of the diagonal of
  // 'worldBounds'; 0 (no occlusion) for empty bounds
  float ambientOcclusionRadius(const box3 &worldBounds) const;
  // 0 'g3d', 1 'sao' (the ssao.frag 'estimator' uniform)
  int ambientOcclusionEstimator() const;
  float ambientOcclusionIntensity() const;
  int ambientOcclusionBlurPasses() const;

 private:
  vec4 m_background{0.f, 0.f, 0.f, 1.f};
  float m_ambientRadiance{1.f};
  vec3 m_ambientColor{1.f};
  float m_eyeLightBlendRatio{0.5f};

  bool m_ignoreAmbientLighting{true};

  bool m_ambientOcclusion{true};
  float m_ambientOcclusionRadius{0.f};
  int m_ambientOcclusionEstimator{0};
  float m_ambientOcclusionIntensity{1.f};
  int m_ambientOcclusionBlurPasses{1};
};

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_SPECIALIZATION(helide_gpu::Renderer *, ANARI_RENDERER);
