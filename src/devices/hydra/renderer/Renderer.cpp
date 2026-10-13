// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Renderer.h"

#include <pxr/imaging/hd/renderDelegate.h>

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>

#include <string>
#include <unordered_map>

PXR_NAMESPACE_USING_DIRECTIVE

namespace anari_hydra {

// Core ANARI renderer parameter -> Hydra render-setting token (SPEC §4.7). Kept
// small and delegate-agnostic; anything else rides the §11.3 verbatim
// passthrough. Values are converted by ANARI type below.
static const std::unordered_map<std::string, TfToken> &TranslationTable()
{
  static const std::unordered_map<std::string, TfToken> table{
      // sample budget: ANARI pixelSamples -> Hydra convergedSamplesPerPixel.
      {"pixelSamples", HdRenderSettingsTokens->convergedSamplesPerPixel},
  };
  return table;
}

// Parameters consumed by the bridge itself, never pushed to the delegate.
static bool IsReservedParam(const std::string &name)
{
  return name == "background" || name == "ambientRadiance"
      || name == "ambientColor";
}

static bool ToVtValue(const helium::AnariAny &a, VtValue &out)
{
  switch (a.type()) {
  case ANARI_BOOL:
    out = VtValue(a.get<bool>());
    return true;
  case ANARI_INT32:
    out = VtValue(a.get<int32_t>());
    return true;
  case ANARI_UINT32:
    out = VtValue(int(a.get<uint32_t>())); // Hydra settings use int
    return true;
  case ANARI_FLOAT32:
    out = VtValue(a.get<float>());
    return true;
  case ANARI_FLOAT32_VEC3: {
    const auto v = a.get<anari::math::float3>();
    out = VtValue(GfVec3f(v.x, v.y, v.z));
    return true;
  }
  case ANARI_FLOAT32_VEC4: {
    const auto v = a.get<anari::math::float4>();
    out = VtValue(GfVec4f(v.x, v.y, v.z, v.w));
    return true;
  }
  case ANARI_STRING:
    out = VtValue(a.getString());
    return true;
  default:
    return false;
  }
}

bool Renderer::coreParamHidesToken(const TfToken &token)
{
  for (const auto &kv : TranslationTable())
    if (kv.second == token)
      return true;
  return false;
}

Renderer::Renderer(HydraGlobalState *s) : Object(ANARI_RENDERER, s)
{
  // helium only commits an object whose parameters changed, so a renderer the
  // app never sets a parameter on must carry the defaults from construction.
  applySettingDefaults();
}

Renderer *Renderer::createInstance(
    std::string_view /*subtype*/, HydraGlobalState *s)
{
  // ANARI defines a single "default" renderer; delegate-specific settings ride
  // ANARI_HYDRA_RENDER_SETTINGS parameters (SPEC §11.3), not subtypes.
  return new Renderer(s);
}

// Element types KHR_RENDERER_BACKGROUND_IMAGE lists for the `background` image.
static bool IsBackgroundImageType(ANARIDataType t)
{
  switch (t) {
  case ANARI_UFIXED8:
  case ANARI_UFIXED8_VEC2:
  case ANARI_UFIXED8_VEC3:
  case ANARI_UFIXED8_VEC4:
  case ANARI_UFIXED8_R_SRGB:
  case ANARI_UFIXED8_RA_SRGB:
  case ANARI_UFIXED8_RGB_SRGB:
  case ANARI_UFIXED8_RGBA_SRGB:
  case ANARI_UFIXED16:
  case ANARI_UFIXED16_VEC2:
  case ANARI_UFIXED16_VEC3:
  case ANARI_UFIXED16_VEC4:
  case ANARI_UFIXED32:
  case ANARI_UFIXED32_VEC2:
  case ANARI_UFIXED32_VEC3:
  case ANARI_UFIXED32_VEC4:
  case ANARI_FLOAT32:
  case ANARI_FLOAT32_VEC2:
  case ANARI_FLOAT32_VEC3:
  case ANARI_FLOAT32_VEC4:
    return true;
  default:
    return false;
  }
}

void Renderer::commitParameters()
{
  // `background` is either a constant colour (KHR_RENDERER_BACKGROUND_COLOR)
  // or an image (KHR_RENDERER_BACKGROUND_IMAGE); each read yields its default
  // when the parameter holds the other form.
  {
    std::lock_guard<std::mutex> lock(m_backgroundMutex);
    m_background = getParam<helium::float4>(
        "background", helium::float4(0.f, 0.f, 0.f, 1.f));
  }
  m_backgroundArray = getParamObject<helium::Array2D>("background");

  // Ambient fill (SPEC §4.7/§6.6): the core `default` renderer's
  // ambientRadiance (intensity) / ambientColor scale a frame-synthesized
  // uniform dome light. Both are bridge-reserved (like `background`) — the
  // Frame reads them, never the delegate. Defaults are no ambient (radiance 0)
  // and white.
  m_ambientRadiance = getParam<float>("ambientRadiance", 0.f);
  m_ambientColor =
      getParam<helium::float3>("ambientColor", helium::float3(1.f, 1.f, 1.f));

  // Distil parameters into the Hydra render-setting map (SPEC §4.7/§11.3).
  // Pass 1: verbatim passthrough under the Hydra token = the parameter name.
  // Pass 2: core translation-table entries, which override any passthrough
  // collision (translation wins).
  m_settings.clear();
  const auto &table = TranslationTable();
  for (auto it = params_begin(); it != params_end(); ++it) {
    const std::string &name = it->first;
    if (IsReservedParam(name) || table.count(name))
      continue;
    VtValue v;
    if (ToVtValue(it->second, v))
      m_settings[TfToken(name)] = v;
    else
      reportMessage(ANARI_SEVERITY_WARNING,
          "renderer setting '%s' has an unsupported type; not pushed to the "
          "delegate (SPEC §11.3)",
          name.c_str());
  }
  for (auto it = params_begin(); it != params_end(); ++it) {
    auto t = table.find(it->first);
    if (t == table.end())
      continue;
    VtValue v;
    if (ToVtValue(it->second, v))
      m_settings[t->second] = v;
  }

  applySettingDefaults();
}

void Renderer::applySettingDefaults()
{
  // The sample budget always reaches the delegate: 1 when unset (the
  // advertised default, favoring interactivity over a delegate's own budget),
  // and anything below 1 falls back to it — a zero budget makes a progressive
  // delegate (HdEmbree) report converged on an empty frame.
  VtValue &samples =
      m_settings[HdRenderSettingsTokens->convergedSamplesPerPixel];
  if (!samples.IsHolding<int>()) {
    samples = VtValue(1);
  } else if (samples.UncheckedGet<int>() < 1) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "renderer 'pixelSamples' is %d; using 1 (must be >= 1)",
        samples.UncheckedGet<int>());
    samples = VtValue(1);
  }

  // An anariRenderFrame is one discrete frame, so the delegate renders
  // non-interactively unless the app overrides it. A delegate that defaults to
  // interactive (hdGatling) otherwise never reports converged and every frame
  // spins out the §8.3 convergence budget.
  m_settings.emplace(HdRenderSettingsTokens->enableInteractive, VtValue(false));
}

void Renderer::finalize()
{
  // Settings are applied per-frame at render time (SPEC §4.7). The background
  // image is decoded here, so both a new array and an in-place edit of the
  // current one (which re-finalizes this observer) take effect. The decode
  // keeps the source's precision (8-bit sources stay compact, HDR and wider
  // sources are Float); the composite reads texels back as float.
  std::shared_ptr<AnariImageData> image;
  if (helium::Array2D *a = m_backgroundArray.get()) {
    if (IsBackgroundImageType(a->elementType())) {
      image = std::make_shared<AnariImageData>();
      if (!DecodeArray2DToImage(a, *image) || image->width <= 0
          || image->height <= 0)
        image.reset();
    } else {
      reportMessage(ANARI_SEVERITY_WARNING,
          "renderer background image has unsupported element type %s; "
          "ignoring it (KHR_RENDERER_BACKGROUND_IMAGE)",
          anari::toString(a->elementType()));
    }
  }
  std::lock_guard<std::mutex> lock(m_backgroundMutex);
  m_backgroundImage = std::move(image);
}

helium::float4 Renderer::background() const
{
  std::lock_guard<std::mutex> lock(m_backgroundMutex);
  return m_background;
}

std::shared_ptr<const AnariImageData> Renderer::backgroundImage() const
{
  std::lock_guard<std::mutex> lock(m_backgroundMutex);
  return m_backgroundImage;
}

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_DEFINITION(anari_hydra::Renderer *);
}
