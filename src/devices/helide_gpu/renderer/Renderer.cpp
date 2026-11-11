// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Renderer.h"
// std
#include <algorithm>
#include <cmath>
#include <string>

namespace helide_gpu {

// Renderer definitions ///////////////////////////////////////////////////////

// 'ambientOcclusionBlurPasses': 1, the single blur that cancels the occlusion
// pass's rotation pattern; the extra a-trous passes are opt-in
constexpr int DEFAULT_AMBIENT_OCCLUSION_BLUR_PASSES = 1;
constexpr int MIN_AMBIENT_OCCLUSION_BLUR_PASSES = 1;
constexpr int MAX_AMBIENT_OCCLUSION_BLUR_PASSES = 5;

// The fraction of the world bounds' diagonal that an
// 'ambientOcclusionRadius' of 0 stands for
constexpr float DERIVED_OCCLUSION_RADIUS_FRACTION = 0.03f;

Renderer::Renderer(HelideGPUDeviceGlobalState *s) : Object(ANARI_RENDERER, s) {}

Renderer *Renderer::createInstance(
    std::string_view /* subtype */, HelideGPUDeviceGlobalState *s)
{
  return new Renderer(s);
}

void Renderer::commitParameters()
{
  m_background = getParam<vec4>("background", vec4(vec3(0.f), 1.f));
  m_ambientRadiance = std::max(getParam<float>("ambientRadiance", 1.f), 0.f);
  m_ambientColor = getParam<vec3>("ambientColor", vec3(1.f));
  m_eyeLightBlendRatio =
      std::clamp(getParam<float>("eyeLightBlendRatio", 0.5f), 0.f, 1.f);
  m_ignoreAmbientLighting = getParam<bool>("ignoreAmbientLighting", true);

  m_ambientOcclusion = getParam<bool>("ambientOcclusion", true);
  m_ambientOcclusionRadius =
      std::max(getParam<float>("ambientOcclusionRadius", 0.f), 0.f);
  const std::string estimator =
      getParamString("ambientOcclusionEstimator", "g3d");
  if (estimator != "g3d" && estimator != "sao") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "unknown ambientOcclusionEstimator '%s', using 'g3d'",
        estimator.c_str());
  }
  m_ambientOcclusionEstimator = estimator == "sao" ? 1 : 0;
  m_ambientOcclusionIntensity =
      std::max(getParam<float>("ambientOcclusionIntensity", 1.f), 0.f);
  m_ambientOcclusionBlurPasses =
      std::clamp(getParam<int32_t>("ambientOcclusionBlurPasses",
                     DEFAULT_AMBIENT_OCCLUSION_BLUR_PASSES),
          MIN_AMBIENT_OCCLUSION_BLUR_PASSES,
          MAX_AMBIENT_OCCLUSION_BLUR_PASSES);
}

void Renderer::finalize()
{
  if (m_ignoreAmbientLighting) {
    m_ambientRadiance = 1.f;
    m_ambientColor = vec3(1.f);
  }
}

vec4 Renderer::background() const
{
  return m_background;
}

float Renderer::ambientRadiance() const
{
  return m_ambientRadiance;
}

vec3 Renderer::ambientColor() const
{
  return m_ambientColor;
}

float Renderer::eyeLightBlendRatio() const
{
  return m_eyeLightBlendRatio;
}

bool Renderer::ambientOcclusion() const
{
  // an intensity of 0 gives an occlusion factor of 1
  return m_ambientOcclusion && m_ambientOcclusionIntensity > 0.f;
}

float Renderer::ambientOcclusionRadius(const box3 &worldBounds) const
{
  if (m_ambientOcclusionRadius > 0.f)
    return m_ambientOcclusionRadius;

  const vec3 diagonal = worldBounds.upper - worldBounds.lower;
  // empty bounds are inverted
  if (!(diagonal.x >= 0.f && diagonal.y >= 0.f && diagonal.z >= 0.f))
    return 0.f;
  const float derived =
      DERIVED_OCCLUSION_RADIUS_FRACTION * glm::length(diagonal);
  return std::isfinite(derived) ? derived : 0.f;
}

int Renderer::ambientOcclusionEstimator() const
{
  return m_ambientOcclusionEstimator;
}

float Renderer::ambientOcclusionIntensity() const
{
  return m_ambientOcclusionIntensity;
}

int Renderer::ambientOcclusionBlurPasses() const
{
  return m_ambientOcclusionBlurPasses;
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_DEFINITION(helide_gpu::Renderer *);
