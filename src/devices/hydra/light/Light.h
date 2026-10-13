// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
// helium
#include "helium/helium_math.h"
// pxr
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>

#include <utility>
#include <vector>

namespace anari_hydra {

using helium::float3;

// The Hydra light sprim a Light subtype maps to (SPEC §6.6): a light-type token
// (distantLight / sphereLight / rectLight / diskLight / domeLight), a world
// transform, and the HdLightTokens parameter list the delegate reads via
// GetLightParamValue. Schema assembly lives in LightBuilder; subtypes only fill
// this.
struct LightData
{
  pxr::TfToken type;
  pxr::GfMatrix4d xform{1};
  std::vector<std::pair<pxr::TfToken, pxr::VtValue>> params;
};

// Base for ANARI lights. v1 maps the six-row subtype table to UsdLux sprims with
// the documented radiometric approximation (SPEC §6.7).
struct Light : public Object
{
  Light(HydraGlobalState *s);
  ~Light() override = default;

  static Light *createInstance(std::string_view subtype, HydraGlobalState *s);

  void commitParameters() override;
  void finalize() override;

  // Device-assigned serial for the light-sprim path taxonomy (SPEC §4.2).
  uint64_t serial() const { return m_serial; }

  // Assemble this light's Hydra sprim description.
  virtual LightData buildLightData() const = 0;

  // True for the hdri (dome) subtype only. Drives the frame's dome
  // camera-visibility policy (SPEC §4.7): a dome light OWNS the backdrop under
  // Storm, so the frame suppresses the skydome unless an hdri wants to be seen.
  virtual bool isHdri() const { return false; }

  // ANARI core light `visible` param (bool, default true). For an hdri dome it
  // decides whether the environment paints the backdrop (visible) or the dome is
  // a pure image-based light letting the renderer's `background` show (SPEC §4.7).
  bool visible() const { return m_visible; }

 protected:
  float3 m_color{1.f, 1.f, 1.f};
  bool m_visible{true};
  uint64_t m_serial{0};
};

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_SPECIALIZATION(anari_hydra::Light *, ANARI_LIGHT);
}
