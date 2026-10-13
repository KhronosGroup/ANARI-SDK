// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "scene/Bounds.h"
#include "spatial_field/SpatialField.h"
// helium
#include "helium/array/Array1D.h"
#include "helium/utility/ChangeObserverPtr.h"
// pxr
#include <pxr/base/gf/vec4f.h>
// std
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace anari_hydra {

// One committed state of a volume, published whole by finalize() so the
// worker reads a consistent classification however stage 1 interleaves with
// a render (SPEC §9.3). The generated shader bakes it (VolumeBuilder).
struct VolumeClassification
{
  helium::IntrusivePtr<SpatialField> field;
  bool visible{true};
  // rgb + color alpha, and opacity, each spread uniformly over valueRange;
  // final alpha = color.a * opacity.
  std::vector<pxr::GfVec4f> colors;
  std::vector<float> opacities;
  float valueLo{0.f};
  float valueHi{1.f};
  float unitDistance{1.f};
};

using VolumeClassificationPtr = std::shared_ptr<const VolumeClassification>;

// ANARI volume -> a Hydra volume rprim bound to its field bprim and to a
// per-volume material whose `volume` terminal is a generated glslfx shader
// that classifies the field sample (SPEC §7, scene/VolumeBuilder.h).
//
// Implemented: transferFunction1D (KHR_VOLUME_TRANSFER_FUNCTION1D).
struct Volume : public Object
{
  static Volume *createInstance(std::string_view subtype, HydraGlobalState *s);

  bool isValid() const override;

  uint64_t serial() const
  {
    return m_serial;
  }

  // The last finalized state, or null before the first. Safe from any thread;
  // a new commit publishes a new object.
  VolumeClassificationPtr classification() const;

  // Object-space bounds: the field's domain, or with `proxy` the box Storm
  // rasterizes (FieldSnapshot::proxyBounds).
  Bounds bounds(bool proxy = false) const;

  // Longest table the shader bakes verbatim; longer arrays are resampled
  // linearly to this many entries (README.md).
  static constexpr size_t kMaxTableSize = 1024;

 protected:
  Volume(HydraGlobalState *s);

  void publish(VolumeClassificationPtr c);

 private:
  uint64_t m_serial{0};
  mutable std::mutex m_mutex;
  VolumeClassificationPtr m_classification;
};

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_SPECIALIZATION(anari_hydra::Volume *, ANARI_VOLUME);
}
