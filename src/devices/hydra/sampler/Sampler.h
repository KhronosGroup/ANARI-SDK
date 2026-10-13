// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "scene/AnariImage.h"
// helium
#include "helium/array/Array1D.h"
#include "helium/array/Array2D.h"
#include "helium/helium_math.h"
#include "helium/utility/ChangeObserverPtr.h"
// std
#include <string>

namespace anari_hydra {

using helium::float4;

// ANARI sampler -> a node inside the material network (SPEC §6.2/§6.4).
// image2D / image1D (1D as an N×1 2D texture) decode their image array into
// the in-memory asset registry (AnariImage) under a unique `anari://` path and
// author a UsdUVTexture fed by a per-binding UsdPrimvarReader_float2 on the
// attribute selected by `inAttribute`. transform and primitive samplers have no
// texture: their value is baked per rprim into a derived primvar (an affine
// map of a per-element attribute, or an array lookup by primitive index) that a
// UsdPrimvarReader_float3 / UsdPrimvarReader_float reads directly.
//
// image3D is a documented exclusion (§6.4): it resolves to an inert
// UnknownObject with a warning (in the factory).
struct Sampler : public Object
{
  ~Sampler() override;

  static Sampler *createInstance(std::string_view subtype, HydraGlobalState *s);

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

  uint64_t serial() const
  {
    return m_serial;
  }
  uint64_t revision() const
  {
    return m_revision;
  }

  // Subtype family. Image samplers author a UsdUVTexture; Transform and
  // Primitive samplers author a derived value primvar per bound rprim and a
  // primvar reader in the network (no texture).
  enum class Kind
  {
    Image,
    Transform,
    Primitive
  };
  Kind kind() const
  {
    return m_kind;
  }

  // The in-memory asset path this sampler's image is registered under; the
  // UsdUVTexture `file` input (SPEC §6.3).
  const std::string &assetPath() const
  {
    return m_assetPath;
  }

  // UsdUVTexture wrap tokens ("repeat" / "clamp" / "mirror" / "black"), mapped
  // from ANARI wrapMode1/wrapMode2 (image2D) or wrapMode (image1D, whose second
  // axis is always "clamp" so the single row is read for any v) (SPEC §6.4).
  //
  // clampToBorder (KHR_SAMPLER_IMAGExD_CLAMP_TO_BORDER) takes one of two exact
  // routes per sampler. Storm's "black" token is GL/Vulkan clamp-to-border
  // with a fixed transparent-black border, so it serves an axis whose effective
  // border texel — borderColor, mixed by outTransform/outOffset when those are
  // baked into the texels — is exactly (0,0,0,0) (a diagonal outTransform's
  // scale/bias then applies to the border like any texel, as ANARI requires).
  // Any other border colour is padded into the registry image instead: one
  // border texel on each side of every clamp-to-border axis, that axis
  // authored "clamp", and the coordinate remap u' = (u·W + 1) / (W + 2) folded
  // into this sampler's input map (below), which reproduces clamp-to-border
  // exactly under nearest and linear filtering (a lookup past the padded edge
  // clamps onto the border texel; the half-texel blend band sits where the
  // border would be sampled).
  const char *wrapS() const
  {
    return m_wrapS;
  }
  const char *wrapT() const
  {
    return m_wrapT;
  }

  // Whether ANARI `filter` selected nearest sampling (SPEC §6.2). "linear" is
  // Storm's default (mipmapped) and needs no authored node params; nearest
  // authors magFilter/minFilter=nearest. The MaterialBuilder branches on this
  // rather than re-parsing a token, mirroring how wrap tokens flow opaquely.
  bool filterIsNearest() const
  {
    return m_filterNearest;
  }

  // --- Input coordinates (SPEC §6.4) --------------------------------------

  // The ANARI attribute feeding the sampler. Slot index in PrimBuilder's
  // element-source table: 0..3 for attributeN, 4 for color, then the geometry-
  // derived sources (object/world position and normal, primitive index), or
  // kAbsentAttributeSlot for every other name. An absent source reads as an
  // absent attribute: the reader fallback for an image sampler (the same as an
  // attributeN the geometry does not author), ANARI's missing-attribute value
  // (0,0,0,1) for a transform sampler.
  static constexpr int kAbsentAttributeSlot = -1;
  static constexpr int kSlotColor = 4;
  static constexpr int kSlotObjectPosition = 5;
  static constexpr int kSlotWorldPosition = 6;
  static constexpr int kSlotObjectNormal = 7;
  static constexpr int kSlotWorldNormal = 8;
  static constexpr int kSlotPrimitiveId = 9;
  static bool IsAttributeSlot(int slot)
  {
    return slot >= 0 && slot <= kSlotColor;
  }
  static bool IsDerivedSlot(int slot)
  {
    return slot > kSlotColor;
  }
  const std::string &inAttribute() const
  {
    return m_inAttribute;
  }
  int inAttributeSlot() const
  {
    return m_inSlot;
  }

  // True when the derived primvar is a function of the rprim's points, so an
  // in-place points mutation must re-author it (Mirror's incremental gate).
  bool readsPositions() const
  {
    return m_inSlot == kSlotObjectPosition || m_inSlot == kSlotWorldPosition;
  }
  // True when the source is world-space, i.e. depends on the surface transform.
  bool readsWorldSpace() const
  {
    return m_inSlot == kSlotWorldPosition || m_inSlot == kSlotWorldNormal;
  }

  // Primvar name the MaterialBuilder points a reader at for an absent
  // inAttribute. No geometry ever authors it, so Storm's
  // UsdPrimvarReader_float2 fallback (0,0) applies on both the UsdTransform2d
  // and baked paths (the PrimBuilder authors no derived primvar for an absent
  // slot either).
  static constexpr const char *kAbsentAttributePrimvar = "anari_absent_st";

  // How an image sampler's inTransform/inOffset reach Storm. Storm's
  // UsdTransform2d evaluates `translation + R(rotation) * (scale * in)` and
  // must sit directly on a primvar reader, so exactly the rotation·scale
  // (orthogonal-column) 2×2 blocks with no z/w-column terms, on a per-vertex
  // attribute slot, map onto it (Transform2d). Every other affine map — shear,
  // terms on the attribute's z/w components — and every geometry-derived source
  // (which has no float2 primvar to read) is baked per element into a derived
  // `st` primvar on each bound rprim (Baked), which is exact for any affine
  // transform because interpolation commutes with it. The map classified here
  // is the composition of a padded border's coordinate remap (wrapS) with the
  // ANARI input map, so a rotation combined with per-axis padding of a
  // non-square image (no longer orthogonal columns) bakes.
  enum class InputMode
  {
    Transform2d,
    Baked
  };
  InputMode inputMode() const
  {
    return m_inMode;
  }

  // Transform2d decomposition in ANARI texture space: t' = R(deg)·(scale⊙t)
  // + bias. The MaterialBuilder folds the ANARI→USD row flip on top.
  float stRotationDegrees() const
  {
    return m_stRotationDeg;
  }
  helium::float2 stScale() const
  {
    return m_stScale;
  }
  helium::float2 stBias() const
  {
    return m_stBias;
  }

  // Baked path: the full input map (ANARI's, then any border-padding remap)
  // applied to a (padded) float4 attribute value, returning the sampled (u,v)
  // in ANARI texture space of the registry image.
  helium::float2 transformInput(const float4 &attribute) const;

  // Whether bound rprims author a derived primvar for this sampler (image
  // samplers on the baked path with a resolvable source; every valid transform
  // or primitive sampler). Mirror records such samplers' revisions per rprim so
  // an edit re-authors the copy.
  bool authorsDerivedPrimvar() const;

  // Name of the derived per-rprim primvar carrying an image sampler's baked
  // coordinates (already in USD st space). Constant per sampler; the rprim
  // re-authors its contents whenever this sampler's revision changes.
  std::string derivedStPrimvar() const
  {
    return "st_s" + std::to_string(m_serial);
  }

  // Name of the derived per-rprim primvar carrying a transform or primitive
  // sampler's value, narrowed to `width` components (3 for a color slot, 1 for
  // a scalar slot — ANARI's first-component rule) so the reader type matches.
  std::string derivedValuePrimvar(int width) const
  {
    return "val_s" + std::to_string(m_serial) + "_float"
        + std::to_string(width);
  }

  // Transform sampler: the full ANARI output map over a (padded) float4
  // attribute value, outTransform * attribute + outOffset.
  float4 transformOutput(const float4 &attribute) const;

  // Primitive sampler: array[primitiveId + inOffset] read as an attribute
  // value, or ANARI's missing-attribute value (0,0,0,1) past the end of the
  // array.
  float4 primitiveValue(uint64_t primitiveId) const;

  // ANARI v=0 addresses image row zero (top). Hio uploads that row at USD v=1,
  // so every coordinate path converts after the ANARI input transform.
  static helium::float2 ToUsdSt(const helium::float2 &t)
  {
    return helium::float2(t.x, 1.f - t.y);
  }

  // --- Output transform (SPEC §6.4) ---------------------------------------

  // outTransform/outOffset. A diagonal outTransform maps onto the UsdUVTexture
  // output `scale`/`bias` (float-exact, values may leave [0,1]); anything that
  // mixes channels is baked into this sampler's decoded texels instead (exact
  // under linear filtering; stored in float, so equally free to leave [0,1]),
  // in which case these report identity.
  float4 outScale() const
  {
    return m_outScale;
  }
  float4 outBias() const
  {
    return m_outBias;
  }

 protected:
  // `imageDims` is 1 or 2 for the image subtypes, 0 for transform/primitive.
  Sampler(HydraGlobalState *s, Kind kind, int imageDims);

  // The sampler holds its image array through a ChangeObserverPtr so an
  // in-place anariUnmapArray re-decodes into the registry and re-marks the
  // mirror.
  helium::ChangeObserverPtr<helium::Array2D> m_image2D;
  helium::ChangeObserverPtr<helium::Array1D> m_image1D;
  // Primitive sampler backing array (§6.4); same observer semantics.
  helium::ChangeObserverPtr<helium::Array1D> m_primitiveArray;
  uint64_t m_primitiveOffset{0};

  Kind m_kind{Kind::Image};
  int m_imageDims{0};
  std::string m_assetPath;
  const char *m_wrapS{"clamp"};
  const char *m_wrapT{"clamp"};
  float4 m_borderColor{0.f, 0.f, 0.f, 0.f};
  bool m_padS{false};
  bool m_padT{false};
  bool m_filterNearest{false};

  std::string m_inAttribute{"attribute0"};
  int m_inSlot{0};
  InputMode m_inMode{InputMode::Transform2d};
  helium::mat4 m_inTransform{linalg::identity};
  float4 m_inOffset{0.f, 0.f, 0.f, 0.f};
  float m_stRotationDeg{0.f};
  helium::float2 m_stScale{1.f, 1.f};
  helium::float2 m_stBias{0.f, 0.f};

  float4 m_outScale{1.f, 1.f, 1.f, 1.f};
  float4 m_outBias{0.f, 0.f, 0.f, 0.f};
  bool m_outBaked{false};
  helium::mat4 m_outTransform{linalg::identity};
  float4 m_outOffset{0.f, 0.f, 0.f, 0.f};

  bool m_valid{false};
  uint64_t m_serial{0};
  uint64_t m_revision{0};

  // Shared parameter readers for the subtypes (SPEC §6.4).
  void commitInAttribute();
  void commitOutTransform();

  // outTransform/outOffset as the per-texel map baked into the registry image
  // when the transform mixes channels (m_outBaked).
  TexelAffine outAffine() const;

  // Decode the current image array (m_imageDims selects which) into the
  // AnariImage registry under m_assetPath, padded per m_padS/m_padT.
  void decodeImage();
};

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_SPECIALIZATION(anari_hydra::Sampler *, ANARI_SAMPLER);
}
