// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Renderer.h"
// std
#include <cmath>
#include <cstdlib>

namespace helide_gpu {

// Renderer definitions ///////////////////////////////////////////////////////

Renderer::Renderer(HelideGPUDeviceGlobalState *s) : Object(ANARI_RENDERER, s)
{
  generateSSAOKernel();
}

Renderer *Renderer::createInstance(
    std::string_view /* subtype */, HelideGPUDeviceGlobalState *s)
{
  return new Renderer(s);
}

void Renderer::commitParameters()
{
  m_background = getParam<vec4>("background", vec4(vec3(0.f), 1.f));
  m_ambientRadiance = getParam<float>("ambientRadiance", 1.f);
  m_eyeLightBlendRatio = getParam<float>("eyeLightBlendRatio", 0.5f);
  m_ignoreAmbientLighting = getParam<bool>("ignoreAmbientLighting", true);

  m_ssaoEnabled = getParam<bool>("ssao", false);
  m_ssaoRadius = getParam<float>("ssao.radius", 0.5f);
  m_ssaoBias = getParam<float>("ssao.bias", 0.025f);
  m_ssaoIntensity = getParam<float>("ssao.intensity", 1.5f);
  m_ssaoKernelSize =
      std::min(std::max(getParam<int>("ssao.samples", 32), 1), 64);
  m_ssaoBlurEnabled = getParam<bool>("ssao.blur", true);
}

void Renderer::finalize()
{
  if (m_ignoreAmbientLighting)
    m_ambientRadiance = 1.f;
}

vec4 Renderer::background() const
{
  return m_background;
}

float Renderer::ambientRadiance() const
{
  return m_ambientRadiance;
}

float Renderer::eyeLightBlendRatio() const
{
  return m_eyeLightBlendRatio;
}

bool Renderer::ssaoEnabled() const
{
  return m_ssaoEnabled;
}

float Renderer::ssaoRadius() const
{
  return m_ssaoRadius;
}

float Renderer::ssaoBias() const
{
  return m_ssaoBias;
}

float Renderer::ssaoIntensity() const
{
  return m_ssaoIntensity;
}

int Renderer::ssaoKernelSize() const
{
  return m_ssaoKernelSize;
}

bool Renderer::ssaoBlurEnabled() const
{
  return m_ssaoBlurEnabled;
}

const vec4 *Renderer::ssaoKernel() const
{
  return m_ssaoKernel.data();
}

void Renderer::generateSSAOKernel()
{
  srand(0);
  for (int i = 0; i < 64; ++i) {
    float x = (float(rand()) / float(RAND_MAX)) * 2.0f - 1.0f;
    float y = (float(rand()) / float(RAND_MAX)) * 2.0f - 1.0f;
    float z = float(rand()) / float(RAND_MAX);

    vec3 sample(x, y, z);
    sample = glm::normalize(sample);

    float scale = float(i) / 64.0f;
    scale = glm::mix(0.1f, 1.0f, scale * scale);
    sample *= scale;

    m_ssaoKernel[i] = vec4(sample, 0.0f);
  }
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_DEFINITION(helide_gpu::Renderer *);
