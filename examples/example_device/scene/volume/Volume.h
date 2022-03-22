// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../../material/Material.h"
#include "../../util/Sampler.h"
#include "SpatialField.h"

namespace anari {
namespace example_device {

struct Volume : public SceneObject
{
  Volume();

  static FactoryMapPtr<Volume> g_volumes;
  static void init();
  static Volume *createInstance(const char *type);

  void commit() override;

  vec3 colorOf(float sample) const;
  float opacityOf(float sample) const;

  const SpatialField *field() const;

  PotentialHit intersect(const Ray &ray, size_t) const override;

 private:
  float normalized(float in) const;

  IntrusivePtr<SpatialField> m_volume;

  box1 m_minmax{0.f, 1.f};
  float m_invSize{0.f};

  Sampler1D<float> m_opacitySampler;
  Sampler1D<vec3> m_colorSampler;

  IntrusivePtr<Array1D> m_colorData;
  IntrusivePtr<Array1D> m_opacityData;
};

// Inlined defintions /////////////////////////////////////////////////////////

inline const SpatialField *Volume::field() const
{
  return m_volume.ptr;
}

inline PotentialHit Volume::intersect(const Ray &ray, size_t volID) const
{
  const void *oldModel = ray.currentSurface;
  ray.currentSurface = this;

  auto hit = m_volume->intersect(ray, volID);

  if (hit) {
    VolumeInfo &vi = getVolumeInfo(hit);
    vi.model = this;
    hit->volID = volID;
  } else
    ray.currentSurface = oldModel;

  return hit;
}

inline vec3 Volume::colorOf(float sample) const
{
  return m_colorSampler.valueAt(normalized(sample));
}

inline float Volume::opacityOf(float sample) const
{
  return m_opacitySampler.valueAt(normalized(sample));
}

inline float Volume::normalized(float sample) const
{
  sample = clamp(sample, m_minmax);
  return (sample - m_minmax.lower) * m_invSize;
}

} // namespace example_device

ANARI_TYPEFOR_SPECIALIZATION(example_device::Volume *, ANARI_VOLUME);

} // namespace anari
