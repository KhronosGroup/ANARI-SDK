// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Sampler.h"
#include "scene/AnariImage.h"
// helium
#include "helium/helium_math.h"

#include <algorithm>
#include <cmath>

namespace anari_hydra {

using anari::math::float2;
using anari::math::float4;

// ANARI wrapMode values (SPEC §6.4). ANARI's default is clampToEdge; `warned`
// flags an unrecognised value, which falls back to clampToEdge.
enum class WrapMode
{
  ClampToEdge,
  Repeat,
  MirrorRepeat,
  ClampToBorder
};

static WrapMode ParseWrapMode(const std::string &m, bool &warned)
{
  if (m == "repeat")
    return WrapMode::Repeat;
  if (m == "mirrorRepeat")
    return WrapMode::MirrorRepeat;
  if (m == "clampToBorder")
    return WrapMode::ClampToBorder;
  if (m != "clampToEdge")
    warned = true;
  return WrapMode::ClampToEdge;
}

// UsdUVTexture wrap token for a wrap mode. clampToBorder is Storm's "black"
// (transparent-black border) unless the axis is padded, in which case the
// padded image is clamped to its border texels (see Sampler::wrapS).
static const char *WrapToken(WrapMode m, bool padded)
{
  switch (m) {
  case WrapMode::Repeat:
    return "repeat";
  case WrapMode::MirrorRepeat:
    return "mirror";
  case WrapMode::ClampToBorder:
    return padded ? "clamp" : "black";
  case WrapMode::ClampToEdge:
    break;
  }
  return "clamp";
}

// Parse an ANARI sampler `filter` string (SPEC §6.2) to "is nearest". ANARI's
// default is "linear"; `warned` flags an unrecognised value, which falls back
// to linear. Mirrors WrapToken's warn-on-unknown pattern.
static bool FilterIsNearest(const std::string &f, bool &warned)
{
  if (f == "nearest")
    return true;
  if (f != "linear")
    warned = true;
  return false; // linear / default
}

// PrimBuilder element-source slot for an ANARI attribute name (0..3 for
// attributeN, 4 for color, the geometry-derived sources after that), or
// Sampler::kAbsentAttributeSlot when the name is unknown.
static int AttributeSlot(const std::string &name)
{
  if (name == "color")
    return Sampler::kSlotColor;
  if (name.size() == 10 && name.compare(0, 9, "attribute") == 0
      && name[9] >= '0' && name[9] <= '3')
    return name[9] - '0';
  if (name == "objectPosition")
    return Sampler::kSlotObjectPosition;
  if (name == "worldPosition")
    return Sampler::kSlotWorldPosition;
  if (name == "objectNormal")
    return Sampler::kSlotObjectNormal;
  if (name == "worldNormal")
    return Sampler::kSlotWorldNormal;
  if (name == "primitiveId")
    return Sampler::kSlotPrimitiveId;
  return Sampler::kAbsentAttributeSlot;
}

// Base ///////////////////////////////////////////////////////////////////////

Sampler::Sampler(HydraGlobalState *s, Kind kind, int imageDims)
    : Object(ANARI_SAMPLER, s),
      m_image2D(this),
      m_image1D(this),
      m_primitiveArray(this),
      m_kind(kind),
      m_imageDims(imageDims)
{
  m_serial = s->nextSerial++;
  m_assetPath = "anari://sampler/s" + std::to_string(m_serial) + ".anari";
}

Sampler::~Sampler()
{
  AnariImageRegistry::instance().erase(m_assetPath);
}

void Sampler::commitInAttribute()
{
  // inAttribute selects the geometry attribute feeding the sampler; each
  // material binding gets its own primvar reader for it (§6.4). An unknown
  // name keeps the authored string for diagnostics but marks the source absent.
  m_inAttribute = getParamString("inAttribute", "attribute0");
  m_inSlot = AttributeSlot(m_inAttribute);
  if (m_inSlot == kAbsentAttributeSlot) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "sampler inAttribute '%s' is not an ANARI attribute; the sampler "
        "reads it as an absent attribute (SPEC §6.4)",
        m_inAttribute.c_str());
  }
}

void Sampler::commitOutTransform()
{
  m_outTransform =
      getParam<helium::mat4>("outTransform", helium::mat4(linalg::identity));
  m_outOffset = getParam<float4>("outOffset", float4(0.f, 0.f, 0.f, 0.f));
}

TexelAffine Sampler::outAffine() const
{
  TexelAffine affine;
  affine.matrix = m_outTransform;
  affine.offset = m_outOffset;
  return affine;
}

void Sampler::commitParameters()
{
  // Image sampler params (SPEC §6.2/§6.4).
  commitInAttribute();

  // outTransform/outOffset (SPEC §6.4): diagonal -> UsdUVTexture output
  // scale/bias; channel mixing -> baked into this sampler's decoded texels.
  // Read first: the wrap decision below needs to know whether the border
  // colour is mixed into the texels too.
  commitOutTransform();
  bool mixes = false;
  for (int column = 0; column < 4; ++column)
    for (int row = 0; row < 4; ++row)
      mixes |= row != column && m_outTransform[column][row] != 0.f;
  m_outBaked = mixes;
  if (mixes) {
    m_outScale = float4(1.f, 1.f, 1.f, 1.f);
    m_outBias = float4(0.f, 0.f, 0.f, 0.f);
  } else {
    m_outScale = float4(m_outTransform[0][0],
        m_outTransform[1][1],
        m_outTransform[2][2],
        m_outTransform[3][3]);
    m_outBias = m_outOffset;
  }

  // wrapMode1/wrapMode2 (image2D) or wrapMode (image1D; its second axis is
  // always clamp so any v reads the single row) and borderColor (SPEC §6.4).
  bool warnWrap = false;
  const WrapMode modeS =
      ParseWrapMode(getParamString(m_imageDims == 1 ? "wrapMode" : "wrapMode1",
                        "clampToEdge"),
          warnWrap);
  const WrapMode modeT = m_imageDims == 1
      ? WrapMode::ClampToEdge
      : ParseWrapMode(getParamString("wrapMode2", "clampToEdge"), warnWrap);
  if (warnWrap) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "sampler wrapMode must be clampToEdge, clampToBorder, repeat or "
        "mirrorRepeat; using clampToEdge (SPEC §6.4)");
  }
  m_borderColor = getParam<float4>("borderColor", float4(0.f, 0.f, 0.f, 0.f));

  // clampToBorder: Storm's transparent-black border serves an axis whose
  // effective border texel is (0,0,0,0) after any texel-baked output map;
  // otherwise the image is padded with the border colour (see Sampler::wrapS).
  int imageW = 0, imageH = 0;
  if (m_imageDims == 2 && m_image2D) {
    imageW = int(m_image2D->size().x);
    imageH = int(m_image2D->size().y);
  } else if (m_imageDims == 1 && m_image1D) {
    imageW = int(m_image1D->size());
    imageH = 1;
  }
  const float4 storedBorder =
      m_outBaked ? outAffine().apply(m_borderColor) : m_borderColor;
  const bool zeroBorder = storedBorder.x == 0.f && storedBorder.y == 0.f
      && storedBorder.z == 0.f && storedBorder.w == 0.f;
  m_padS = modeS == WrapMode::ClampToBorder && !zeroBorder && imageW > 0;
  m_padT = modeT == WrapMode::ClampToBorder && !zeroBorder && imageH > 0;
  m_wrapS = WrapToken(modeS, m_padS);
  m_wrapT = WrapToken(modeT, m_padT);
  // The padding remap in ANARI texture space, composed onto the input map
  // below: u' = (u·W + 1) / (W + 2) maps the source's [0,1] onto the interior
  // texels of the padded image.
  helium::mat4 pad(linalg::identity);
  float4 padOffset(0.f, 0.f, 0.f, 0.f);
  if (m_padS) {
    pad[0][0] = float(imageW) / float(imageW + 2);
    padOffset.x = 1.f / float(imageW + 2);
  }
  if (m_padT) {
    pad[1][1] = float(imageH) / float(imageH + 2);
    padOffset.y = 1.f / float(imageH + 2);
  }

  // filter -> UsdUVTexture magFilter/minFilter (SPEC §6.2). Default is linear.
  bool warnFilter = false;
  m_filterNearest =
      FilterIsNearest(getParamString("filter", "linear"), warnFilter);
  if (warnFilter) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "sampler filter must be 'nearest' or 'linear'; using linear "
        "(SPEC §6.2)");
  }

  // inTransform/inOffset (SPEC §6.4). The sampled coordinate is the xy of
  // inTransform * attribute + inOffset (attribute padded to (x,y,0,1)). The 2×2
  // block [a b; c d] (helium mat4 is column-major: m[column][row]) is a
  // rotation·scale exactly when its columns are orthogonal: column 0 =
  // sx·(cos θ, sin θ), column 1 = sy·(−sin θ, cos θ). That, with no z/w-column
  // terms, is what a UsdTransform2d can express; anything else is baked.
  // The classified and baked map is the ANARI map followed by the padding
  // remap: pad · (inTransform · a + inOffset) + padOffset.
  const helium::mat4 anariTransform =
      getParam<helium::mat4>("inTransform", helium::mat4(linalg::identity));
  const float4 anariOffset =
      getParam<float4>("inOffset", float4(0.f, 0.f, 0.f, 0.f));
  m_inTransform = linalg::mul(pad, anariTransform);
  m_inOffset = linalg::mul(pad, anariOffset) + padOffset;
  {
    const float a = m_inTransform[0][0], c = m_inTransform[0][1];
    const float b = m_inTransform[1][0], d = m_inTransform[1][1];
    float theta = 0.f, sx = std::sqrt(a * a + c * c), sy = 0.f;
    float residual = 0.f;
    if (sx > 0.f) {
      theta = std::atan2(c, a);
      const float cs = a / sx, sn = c / sx;
      sy = -b * sn + d * cs;
      residual = std::fabs(b + sy * sn) + std::fabs(d - sy * cs);
    } else {
      sy = std::sqrt(b * b + d * d);
      if (sy > 0.f)
        theta = std::atan2(-b, d);
    }
    const float magnitude =
        std::max({1.f, std::fabs(a), std::fabs(b), std::fabs(c), std::fabs(d)});
    const bool zwTerms = m_inTransform[2][0] != 0.f
        || m_inTransform[2][1] != 0.f || m_inTransform[3][0] != 0.f
        || m_inTransform[3][1] != 0.f;
    // A geometry-derived source has no float2 primvar for a UsdTransform2d to
    // read, so it always bakes (the derived st primvar is its only carrier).
    // An absent source keeps the Transform2d path, like an unauthored
    // attributeN, so both read the same reader fallback texel.
    const bool decomposable =
        residual <= 1e-5f * magnitude && !zwTerms && !IsDerivedSlot(m_inSlot);
    m_inMode = decomposable ? InputMode::Transform2d : InputMode::Baked;
    m_stRotationDeg = theta * 180.f / float(M_PI);
    m_stScale = float2(sx, sy);
    m_stBias = float2(m_inOffset.x, m_inOffset.y);
  }
}

float2 Sampler::transformInput(const float4 &attribute) const
{
  const float4 t = linalg::mul(m_inTransform, attribute) + m_inOffset;
  return float2(t.x, t.y);
}

float4 Sampler::transformOutput(const float4 &attribute) const
{
  return linalg::mul(m_outTransform, attribute) + m_outOffset;
}

float4 Sampler::primitiveValue(uint64_t primitiveId) const
{
  const helium::Array1D *a = m_primitiveArray.get();
  const uint64_t i = primitiveId + m_primitiveOffset;
  if (!a || i < primitiveId || i >= a->size())
    return float4(0.f, 0.f, 0.f, 1.f);
  return a->readAsAttributeValue(int(i));
}

bool Sampler::authorsDerivedPrimvar() const
{
  if (!m_valid)
    return false;
  if (m_kind == Kind::Image)
    return m_inMode == InputMode::Baked && m_inSlot != kAbsentAttributeSlot;
  return true;
}

void Sampler::finalize()
{
  // Array observers schedule finalization without re-reading parameters.
  // A new asset identity also invalidates delegates' file-keyed texture caches.
  // The revision is one counter for every kind of change, so an image-only
  // unmap also re-authors rprims carrying this sampler's baked st primvar.
  AnariImageRegistry::instance().erase(m_assetPath);
  ++m_revision;
  m_assetPath = "anari://sampler/s" + std::to_string(m_serial) + "_r"
      + std::to_string(m_revision) + ".anari";
  decodeImage();
  deviceState()->mirrorDirty.mark(this);
}

bool Sampler::isValid() const
{
  return m_valid;
}

void Sampler::decodeImage()
{
  // Decode any supported element type to linear RGBA through helium's
  // readAsAttributeValue (copy-at-load into the registry — outside the
  // zero-copy scope, SPEC §6.3), keyed by this sampler's asset path, at the
  // precision TexelFormatFor picks for the source. A channel-mixing
  // outTransform is applied per texel here (§6.4): the registry copy is this
  // sampler's own, so other samplers sharing the array are unaffected, and the
  // result is stored in float, so it may leave [0,1].
  const TexelAffine affine = outAffine();
  const TexelAffine *transform = m_outBaked ? &affine : nullptr;
  // A nonzero clamp-to-border colour frames the padded axes (§6.4); the same
  // transform applies to the border texel as to the source texels.
  TexelBorder border;
  border.padX = m_padS;
  border.padY = m_padT;
  border.color = m_borderColor;
  const TexelBorder *pad = (m_padS || m_padT) ? &border : nullptr;

  AnariImageData data;
  if (m_imageDims == 2) {
    // Shared decode with the hdri dome light (SPEC §6.3).
    m_valid = DecodeArray2DToImage(
        m_image2D.get(), data, transform, TexelFormat::Float, pad);
  } else if (m_imageDims == 1) {
    // image1D as an N×1 2D texture (SPEC §6.4).
    m_valid = DecodeArray1DToImage(
        m_image1D.get(), data, transform, TexelFormat::Float, pad);
  } else {
    m_valid = false;
  }

  if (m_valid)
    AnariImageRegistry::instance().set(m_assetPath, std::move(data));
  else
    AnariImageRegistry::instance().erase(m_assetPath);
}

// Subtypes
// /////////////////////////////////////////////////////////////////////

namespace {

struct Image2D : public Sampler
{
  Image2D(HydraGlobalState *s) : Sampler(s, Kind::Image, 2) {}
  void commitParameters() override
  {
    m_image2D = getParamObject<helium::Array2D>("image");
    Sampler::commitParameters();
  }
};

struct Image1D : public Sampler
{
  Image1D(HydraGlobalState *s) : Sampler(s, Kind::Image, 1) {}
  void commitParameters() override
  {
    m_image1D = getParamObject<helium::Array1D>("image");
    Sampler::commitParameters();
  }
};

// transform: value = outTransform * inAttribute + outOffset (KHR_SAMPLER_
// TRANSFORM has no image, wrap, filter or input transform). Baked per element
// into a derived value primvar on each bound rprim (SPEC §6.4).
struct Transform : public Sampler
{
  Transform(HydraGlobalState *s) : Sampler(s, Kind::Transform, 0) {}
  void commitParameters() override
  {
    commitInAttribute();
    commitOutTransform();
  }
  void finalize() override
  {
    m_valid = true;
    ++m_revision;
    deviceState()->mirrorDirty.mark(this);
  }
};

// primitive: value = array[primitiveId + inOffset], read as an attribute value
// (KHR_SAMPLER_PRIMITIVE). Baked into a uniform-rate derived value primvar on
// each bound rprim, mapped through the geometry's Hydra-face -> ANARI-primitive
// table so tessellated geometry and curves index by ANARI primitive (§6.4).
struct Primitive : public Sampler
{
  Primitive(HydraGlobalState *s) : Sampler(s, Kind::Primitive, 0) {}
  void commitParameters() override
  {
    m_primitiveArray = getParamObject<helium::Array1D>("array");
    m_primitiveOffset = getParam<uint64_t>("inOffset", 0);
    // The subtype has no inAttribute parameter; its source is the primitive
    // index by definition (keeps inAttribute()/slot diagnostics truthful).
    m_inAttribute = "primitiveId";
    m_inSlot = kSlotPrimitiveId;
    if (!m_primitiveArray) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "primitive sampler is missing its required 'array' parameter");
    }
  }
  void finalize() override
  {
    // Array observers schedule finalization without re-reading parameters; the
    // revision covers in-place unmap edits of the array too.
    m_valid = m_primitiveArray.get() != nullptr;
    ++m_revision;
    deviceState()->mirrorDirty.mark(this);
  }
};

} // namespace

Sampler *Sampler::createInstance(std::string_view subtype, HydraGlobalState *s)
{
  if (subtype == "image2D")
    return new Image2D(s);
  else if (subtype == "image1D")
    return new Image1D(s);
  else if (subtype == "transform")
    return new Transform(s);
  else if (subtype == "primitive")
    return new Primitive(s);
  // image3D is a documented exclusion (SPEC §6.4); the device turns a null
  // here into an inert UnknownObject with a warning.
  return nullptr;
}

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_DEFINITION(anari_hydra::Sampler *);
}
