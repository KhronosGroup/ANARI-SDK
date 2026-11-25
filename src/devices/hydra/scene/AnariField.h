// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The in-memory asset scheme for spatial fields. A process-global registry maps
// an `anari://field/…​.anari` asset path to a dense float32 scalar grid; a
// self-registered HioFieldTextureData plugin (AnariField.cpp, compiled into the
// device library, declared in plugInfo.json) serves that grid straight to
// HdStorm's field-texture pipeline, dispatched purely by the `anari` extension
// — no file on disk, no Ar resolution, no OpenVDB. This is the image scheme's
// sibling (AnariImage.h): copy-at-load into the registry is permitted (outside
// the zero-copy scope).

#pragma once

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace anari_hydra {

// One dense, vertex-centred scalar grid: value (i,j,k) sits at object-space
// position origin + (i,j,k) * spacing, x fastest (ANARI Array3D order).
// Storm places texel centres on those positions (the served bounding box is
// the grid's index range widened by half a voxel), so its trilinear lookup
// reproduces ANARI's vertex-centred interpolation inside the domain. Published
// grids always have positive spacing (a negative ANARI spacing is reordered).
struct AnariFieldData
{
  pxr::GfVec3i dims{0, 0, 0};
  pxr::GfVec3f origin{0.f, 0.f, 0.f};
  pxr::GfVec3f spacing{1.f, 1.f, 1.f};
  std::vector<float> values;
  // A volume's classification table (scene/VolumeBuilder.h) rather than a
  // spatial grid: the shader reads it texel by texel with texelFetch, so its
  // placement only matters to Storm, which merges every field a volume binds
  // into one ray-marched box and steps at the smallest field spacing. A table
  // is therefore served covering exactly [boxLo, boxHi] (the value field's
  // proxy box), with a matrix whose columns are as long as that box: the box
  // and the step size stay the value field's.
  bool table{false};
  pxr::GfVec3f boxLo{0.f, 0.f, 0.f};
  pxr::GfVec3f boxHi{0.f, 0.f, 0.f};
};

using AnariFieldDataPtr = std::shared_ptr<const AnariFieldData>;

// Process-global field store keyed by asset path. An entry lives exactly as
// long as the FieldSnapshot that registered it (spatial_field/SpatialField.h),
// which the mirror holds while a field bprim names the path; grids are shared
// and immutable, so a texture load that already fetched one keeps it alive.
class AnariFieldRegistry
{
 public:
  static AnariFieldRegistry &instance();

  void set(const std::string &path, AnariFieldDataPtr data);
  void erase(const std::string &path);
  AnariFieldDataPtr get(const std::string &path) const;

 private:
  mutable std::mutex m_mutex;
  std::map<std::string, AnariFieldDataPtr> m_fields;
};

} // namespace anari_hydra
