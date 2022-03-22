// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "AmbientOcclusion.h"

namespace anari {
namespace example_device {

#define ulpEpsilon 0x1.fp-21

static float epsilonFrom(const vec3 &P, const vec3 &dir, float t)
{
  return glm::compMax(vec4(abs(P), glm::compMax(abs(dir)) * t)) * ulpEpsilon;
}

static void rayMarchVolume(const Ray &ray,
    const Hit &hit,
    float volumeStepFactor,
    vec3 &color,
    float &opacity)
{
  box1 currentInterval = hit.t;

  auto &info = getVolumeInfo(hit);
  const auto *model = info.model;
  const auto *field = model->field();
  const float stepSize = field->stepSize() * volumeStepFactor;

  currentInterval.lower += (stepSize * 0.1f);

  while (opacity < 0.99f && size(currentInterval) >= 0.f) {
    const vec3 p = ray.org + ray.dir * currentInterval.lower;

    const float s = field->sampleAt(p);
    if (!std::isnan(s)) {
      vec3 c = model->colorOf(s);
      float o = model->opacityOf(s);

      color += stepSize * (1.f - opacity) * c * o;
      opacity += stepSize * (1.f - opacity) * o;
    }

    currentInterval.lower += stepSize;
  }
}

static float computeAO(const Ray &ray,
    const Hit &hit,
    const vec3 &normal,
    const int numSamples,
    const World &world)
{
  const vec3 hitP = ray.org + (hit.t.lower * ray.dir);
  const float epsilon = epsilonFrom(hitP, ray.dir, hit.t.lower);

  int hits = 0;
  for (int i = 0; i < numSamples; i++) {
    auto ao_dir = randomDir();

    const bool forwardDir = dot(ao_dir, normal) > 0.f;
    if (!forwardDir)
      ao_dir = -ao_dir;

    Ray ao_ray;
    ao_ray.org = hitP + (epsilon * normal);
    ao_ray.dir = normalize(ao_dir);
    ao_ray.skipVolumes = true;

    if (isOccluded(ao_ray, world))
      hits++;
  }

  return 1.f - hits / float(numSamples);
}

// AmbientOcclusion defintions ////////////////////////////////////////////////

ANARIParameter AmbientOcclusion::g_parameters[] = {
    {"backgroundColor", ANARI_FLOAT32_VEC4},
    {"pixelSamples", ANARI_INT32},
    {"volumeSamplingRate", ANARI_FLOAT32},
    {"aoSamples", ANARI_INT32},
    {NULL, ANARI_UNKNOWN}
};

void AmbientOcclusion::commit()
{
  m_volumeStepFactor = 1.f / getParam<float>("volumeSamplingRate", 0.125f);
  m_sampleCount = getParam<int>("aoSamples", 1);
  Renderer::commit();
}

RenderedSample AmbientOcclusion::renderSample(Ray ray, const World &world) const
{
  RenderedSample retval;

  vec3 superColor(0);
  float opacity = 0.f;

  int numVolumesHit = 0;

  constexpr float MaxOpacity = 0.99f;

  while (opacity < MaxOpacity) {
    auto hit = closestHit(ray, world);

    if (hit)
      retval.depth = std::min(retval.depth, hit->t.lower);

    if (hitGeometry(hit)) {
      GeometryInfo &info = getGeometryInfo(hit);

      vec3 color(info.color.x, info.color.y, info.color.z);

      if (info.material)
        color *= info.material->diffuse();

      color *= dot(info.normal, -ray.dir);

      if (m_sampleCount > 0)
        color *= computeAO(ray, *hit, info.normal, m_sampleCount, world);

      superColor += (1.f - opacity) * color;
      opacity = 1.f;

      ray.t.lower = hit->t.upper + 1e-3f;
    } else if (hitVolume(hit)) {
      rayMarchVolume(ray, *hit, m_volumeStepFactor, superColor, opacity);
      ray.t.lower = hit->t.upper + 1e-6f;
      ray.skipVolumes = true;
      if (opacity >= MaxOpacity) { // as we won't enter the loop again
        superColor += vec3((1.f - opacity) * m_bgColor);
        opacity += (1.f - opacity) * m_bgColor.w;
      }
    } else {
      superColor += vec3((1.f - opacity) * m_bgColor);
      opacity += (1.f - opacity) * m_bgColor.w;
      break;
    }
  }

  retval.color = vec4(superColor, opacity);

  return retval;
}

} // namespace example_device
} // namespace anari
