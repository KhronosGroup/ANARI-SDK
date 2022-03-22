// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SceneObject.h"

namespace anari {
namespace example_device {

struct SpatialField : public SceneObject
{
  SpatialField();

  static SpatialField *createInstance(const char *type);
  static FactoryMapPtr<SpatialField> g_spatialFields;
  static void init();
  static ANARIParameter *parameters(const char *type);

  virtual float sampleAt(const vec3 &coord) const = 0;

  float stepSize() const;

 protected:
  void setStepSize(float size);

 private:
  float m_stepSize{0.f};
};

struct SpatialFieldBox : public Primitive
{
  box3 m_bounds;

  SpatialFieldBox() = default;
  SpatialFieldBox(const box3 &b) : m_bounds(b) {}

  box3 bounds() const override
  {
    return m_bounds;
  }

  PotentialHit intersect(const Ray &ray, size_t primID) const override
  {
    const vec3 mins = (m_bounds.lower - ray.org) * (1.f / ray.dir);
    const vec3 maxs = (m_bounds.upper - ray.org) * (1.f / ray.dir);
    const vec3 nears = min(mins, maxs);
    const vec3 fars = max(mins, maxs);

    box1 t(glm::compMax(nears), glm::compMin(fars));

    if (t.lower < t.upper) {
      VolumeInfo info;
      t.lower = clamp(t.lower, ray.t);
      t.upper = clamp(t.upper, ray.t);
      return makeHit(t, info);
    }

    return std::nullopt;
  }
};

// Inlined definitions ////////////////////////////////////////////////////////

inline float SpatialField::stepSize() const
{
  return m_stepSize;
}

} // namespace example_device

ANARI_TYPEFOR_SPECIALIZATION(example_device::SpatialField *, ANARI_SPATIAL_FIELD);

} // namespace anari
