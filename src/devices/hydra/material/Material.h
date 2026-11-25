// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "sampler/Sampler.h"
// helium
#include "helium/helium_math.h"
#include "helium/utility/IntrusivePtr.h"
// pxr
#include <pxr/base/tf/token.h>
// std
#include <vector>

namespace anari_hydra {

using helium::float3;
using helium::float4;

// A material input driven by a sampler instead of a constant: the
// MaterialBuilder authors a UsdUVTexture for `sampler` and connects `output` of
// it (rgb for a color slot, r/a for a scalar slot) to `usdInput`.
//
// `normalMap` flags the `normal` slot. Per ANARI the sampler's value is already
// the [-1,1] tangent-space normal — the app decodes texels through
// outTransform/outOffset — so no unpack is added. An image sampler feeds a
// UsdUVTexture as on any slot; a transform/primitive sampler's baked primvar
// feeds a generated Storm surface shader. Storm synthesizes the TBN frame from
// `st` — no `tangents` primvar is needed (proven in
// prototypes/015-normal-map-tangents).
struct SamplerBinding
{
  pxr::TfToken usdInput; // UsdPreviewSurface input, e.g. "diffuseColor"
  pxr::TfToken output; // UsdUVTexture output: "rgb", "r", or "a"
  helium::IntrusivePtr<Sampler> sampler;
  bool normalMap{false}; // the `normal` slot

  // Component count a transform/primitive sampler bakes for this slot and its
  // reader reads: float3 for a color slot, the first component for a scalar
  // one. One place, because the derived primvar name embeds it.
  int valueWidth() const
  {
    return output == pxr::TfToken("rgb") ? 3 : 1;
  }
};

// A material input driven by a geometry attribute instead of a constant: the
// parameter names an ANARI attribute (a STRING, e.g. "color" or "attribute0").
// The MaterialBuilder authors a UsdPrimvarReader_* node reading `primvar` and
// connects its `result` output to `usdInput`; the reader type (float3 vs float)
// follows the input slot. `primvar` matches the per-vertex primvar PrimBuilder
// authors for that attribute.
struct AttributeBinding
{
  pxr::TfToken usdInput; // UsdPreviewSurface input, e.g. "diffuseColor"
  std::string primvar; // per-vertex primvar name: "color" or "attribute0..3"
};

// Base for ANARI materials. The device authors a UsdPreviewSurface-shaped
// material network from these parameters as a material sprim, and mirrors the
// constant color as displayColor for delegates that ignore networks (Embree).
struct Material : public Object
{
  Material(HydraGlobalState *s);
  ~Material() override = default;

  static Material *createInstance(
      std::string_view subtype, HydraGlobalState *s);

  void commitParameters() override;
  void finalize() override;

  // Device-assigned serial for the material-sprim path taxonomy.
  uint64_t serial() const
  {
    return m_serial;
  }

  // How ANARI alphaMode is interpreted at the UsdPreviewSurface
  // opacity/opacityThreshold inputs.
  enum class AlphaMode
  {
    Opaque,
    Blend,
    Mask
  };

  // UsdPreviewSurface parameter block, filled by subtypes.
  float3 color() const
  {
    return m_color;
  } // -> diffuseColor
  float opacity() const
  {
    return m_opacity;
  } // -> opacity
  float metallic() const
  {
    return m_metallic;
  }
  float roughness() const
  {
    return m_roughness;
  }
  float3 emissive() const
  {
    return m_emissive;
  } // -> emissiveColor
  float ior() const
  {
    return m_ior;
  }
  float clearcoat() const
  {
    return m_clearcoat;
  }
  float clearcoatRoughness() const
  {
    return m_clearcoatRoughness;
  }
  float occlusion() const
  {
    return m_occlusion;
  }
  AlphaMode alphaMode() const
  {
    return m_alphaMode;
  }
  float opacityThreshold() const
  {
    return m_opacityThreshold;
  }

  // Inputs bound to a sampler rather than a constant. Empty for the
  // constant-only case.
  const std::vector<SamplerBinding> &samplerBindings() const
  {
    return m_samplerBindings;
  }

  // Inputs bound to a geometry attribute rather than a constant.
  const std::vector<AttributeBinding> &attributeBindings() const
  {
    return m_attributeBindings;
  }

 protected:
  // If `name` names a sampler param, record a binding of `output` of it to
  // `usdInput` and return true (the constant read should then be skipped). Used
  // by subtypes for each texturable slot. `normalMap` marks the `normal` slot,
  // whose transform/primitive samplers need the generated surface.
  bool bindSampler(const char *name,
      const pxr::TfToken &usdInput,
      const pxr::TfToken &output,
      bool normalMap = false);

  // If `name` is set to a STRING naming a supported ANARI attribute (color /
  // attribute0..3), record a binding of that attribute's primvar to `usdInput`
  // and return true (the constant read should then be skipped). An unrecognized
  // attribute string is treated as "none": no binding, warned, returns false.
  bool bindAttribute(const char *name, const pxr::TfToken &usdInput);

  std::vector<SamplerBinding> m_samplerBindings;
  std::vector<AttributeBinding> m_attributeBindings;

  float3 m_color{0.8f, 0.8f, 0.8f};
  float m_opacity{1.f};
  float m_metallic{0.f};
  float m_roughness{1.f};
  float3 m_emissive{0.f, 0.f, 0.f};
  float m_ior{1.5f};
  float m_clearcoat{0.f};
  float m_clearcoatRoughness{0.f};
  float m_occlusion{1.f};
  AlphaMode m_alphaMode{AlphaMode::Opaque};
  float m_opacityThreshold{0.f};
  uint64_t m_serial{0};
};

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_SPECIALIZATION(anari_hydra::Material *, ANARI_MATERIAL);
}
