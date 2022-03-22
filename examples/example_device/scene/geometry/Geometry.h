// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SceneObject.h"

namespace anari {
namespace example_device {

struct Geometry : public SceneObject
{
  Geometry();

  static Geometry *createInstance(const char *type);
  static FactoryMapPtr<Geometry> g_geometries;
  static void init();

  void commit() override;

  void populateHitColor(GeometryInfo &gi, size_t primID) const;

 protected:
  IntrusivePtr<Array1D> m_colorArray;
  vec4 *m_color{nullptr};
};

inline void Geometry::populateHitColor(GeometryInfo &gi, size_t primID) const
{
  if (m_color)
    gi.color = m_color[primID];
}

} // namespace example_device

ANARI_TYPEFOR_SPECIALIZATION(example_device::Geometry *, ANARI_GEOMETRY);

} // namespace anari
