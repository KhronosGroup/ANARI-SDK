// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "scene/AnariImage.h"
#include "helium/array/Array2D.h"
#include "helium/helium_math.h"
#include "helium/utility/ChangeObserverPtr.h"
// pxr
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
// std
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace anari_hydra {

// The ANARI renderer is a settings profile applied to the frame's delegate
// (SPEC §4.7), not a delegate instance. commitParameters distils the object's
// parameters into a Hydra render-setting map the Frame pushes via
// HdRenderDelegate::SetRenderSetting: a small core translation table (ANARI
// name -> Hydra token) plus delegate-native passthrough under the verbatim
// Hydra token (ANARI_HYDRA_RENDER_SETTINGS, §11.3). Translation wins collisions.
struct Renderer : public Object
{
  Renderer(HydraGlobalState *s);
  ~Renderer() override = default;

  static Renderer *createInstance(
      std::string_view subtype, HydraGlobalState *s);

  void commitParameters() override;
  void finalize() override;

  helium::float4 background() const;

  // KHR_RENDERER_BACKGROUND_IMAGE (SPEC §4.7): `background` given as an
  // ARRAY2D, decoded to linear RGBA in finalize (which re-runs on an
  // in-place array edit). Null when `background` is a constant colour or the
  // array's element type is not a colour type. The frame snapshots the pointer
  // per render and composites it under the color AOV on readback; the decoded
  // image is immutable, so a snapshot outlives later renderer commits.
  std::shared_ptr<const AnariImageData> backgroundImage() const;

  // Ambient fill (SPEC §4.7/§6.6): intensity/color of the frame-synthesized
  // uniform dome light. ambientRadiance <= 0 means no ambient fill.
  float ambientRadiance() const { return m_ambientRadiance; }
  helium::float3 ambientColor() const { return m_ambientColor; }

  // True if a delegate render-setting token is the Hydra target of a core
  // translation-table alias, so it must not be reflected verbatim (translation
  // wins the collision, §11.3). The bridge's own renderer parameters are
  // reflected from the generated introspection table (Introspection.h).
  static bool coreParamHidesToken(const pxr::TfToken &token);

  // Hydra render settings distilled from this renderer's parameters, keyed by
  // Hydra token. Applied per-render to the frame's delegate (SPEC §4.7).
  const std::map<pxr::TfToken, pxr::VtValue> &renderSettings() const
  {
    return m_settings;
  }

 private:
  // Fill in the settings every render carries unless the app overrides them.
  void applySettingDefaults();

  helium::float4 m_background{0.f, 0.f, 0.f, 1.f};
  helium::ChangeObserverPtr<helium::Array2D> m_backgroundArray{this};
  // Guards m_background and m_backgroundImage: commitParameters/finalize can
  // run on an app thread (a WAIT query flushes the commit buffer) while the
  // worker reads them for a render.
  mutable std::mutex m_backgroundMutex;
  std::shared_ptr<const AnariImageData> m_backgroundImage;
  float m_ambientRadiance{0.f};
  helium::float3 m_ambientColor{1.f, 1.f, 1.f};
  std::map<pxr::TfToken, pxr::VtValue> m_settings;
};

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_SPECIALIZATION(anari_hydra::Renderer *, ANARI_RENDERER);
}
