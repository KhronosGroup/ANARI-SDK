// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Light.h"
#include "scene/AnariImage.h"
// helium
#include "helium/array/Array2D.h"
#include "helium/utility/ChangeObserverPtr.h"

#include <pxr/imaging/hd/light.h>
#include <pxr/imaging/hd/tokens.h>

#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>

#include <pxr/usd/sdf/assetPath.h>

#include <cmath>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

namespace anari_hydra {

// A fixed factor turning ANARI point/spot radiant-intensity (W/sr) into a
// delegate 'intensity' — an explicit, documented approximation (SPEC §6.7);
// absolute radiometric parity is not chased in v1.
static constexpr float kPointIntensityScale = 1.f;

static GfVec3f ToVec3f(const float3 &c)
{
  return GfVec3f(c[0], c[1], c[2]);
}

// Local-to-world orienting a light's local -Z onto `dir` (distant/spot/disk).
static GfMatrix4d OrientToDirection(const float3 &dir)
{
  GfVec3d d(dir[0], dir[1], dir[2]);
  if (d.GetLength() < 1e-8)
    d = GfVec3d(0, 0, -1);
  d.Normalize();
  return GfMatrix4d(GfRotation(GfVec3d(0, 0, -1), d), GfVec3d(0));
}

static GfMatrix4d TranslateRotate(const float3 &pos, const float3 &dir)
{
  GfMatrix4d m = OrientToDirection(dir);
  m.SetTranslateOnly(GfVec3d(pos[0], pos[1], pos[2]));
  return m;
}

// Base ///////////////////////////////////////////////////////////////////////

Light::Light(HydraGlobalState *s) : Object(ANARI_LIGHT, s)
{
  m_serial = s->nextSerial++;
}

void Light::commitParameters()
{
  m_color = getParam<float3>("color", float3(1.f, 1.f, 1.f));
  // ANARI core light `visible` (default true). Read for every light; only the
  // hdri subtype consults it today (the frame's dome camera-visibility policy,
  // SPEC §4.7), so it is inert for the others.
  m_visible = getParam<bool>("visible", true);
}

void Light::finalize()
{
  deviceState()->mirrorDirty.mark(this);
}

// Subtypes /////////////////////////////////////////////////////////////////////

namespace {

// directional -> distant light (SPEC §6.6): irradiance -> intensity, oriented
// so the distant light's -Z axis follows the ANARI direction.
struct Directional : public Light
{
  Directional(HydraGlobalState *s) : Light(s) {}
  float3 m_direction{0.f, 0.f, -1.f};
  float m_irradiance{1.f};

  void commitParameters() override
  {
    Light::commitParameters();
    m_direction = getParam<float3>("direction", float3(0.f, 0.f, -1.f));
    m_irradiance = getParam<float>("irradiance", getParam<float>("radiance", 1.f));
  }

  LightData buildLightData() const override
  {
    LightData d;
    d.type = HdPrimTypeTokens->distantLight;
    d.xform = OrientToDirection(m_direction);
    d.params = {{HdLightTokens->intensity, VtValue(m_irradiance)},
        {HdLightTokens->color, VtValue(ToVec3f(m_color))},
        {HdLightTokens->angle, VtValue(0.53f)},
        {HdLightTokens->normalize, VtValue(true)}};
    return d;
  }
};

// point -> sphere light (SPEC §6.6): treatAsPoint when radius is 0.
struct Point : public Light
{
  Point(HydraGlobalState *s) : Light(s) {}
  float3 m_position{0.f, 0.f, 0.f};
  float m_intensity{1.f};
  float m_radius{0.f};

  void commitParameters() override
  {
    Light::commitParameters();
    m_position = getParam<float3>("position", float3(0.f, 0.f, 0.f));
    m_intensity = getParam<float>("intensity", getParam<float>("power", 1.f));
    m_radius = getParam<float>("radius", 0.f);
  }

  LightData buildLightData() const override
  {
    LightData d;
    d.type = HdPrimTypeTokens->sphereLight;
    d.xform = GfMatrix4d(1).SetTranslate(
        GfVec3d(m_position[0], m_position[1], m_position[2]));
    // treatAsPoint when there is no radius (SPEC §6.6).
    d.params = {{HdLightTokens->intensity,
                    VtValue(m_intensity * kPointIntensityScale)},
        {HdLightTokens->color, VtValue(ToVec3f(m_color))},
        {HdLightTokens->radius, VtValue(m_radius)},
        {TfToken("treatAsPoint"), VtValue(m_radius == 0.f)}};
    return d;
  }
};

// spot -> sphere light + shaping cone (SPEC §6.6).
struct Spot : public Light
{
  Spot(HydraGlobalState *s) : Light(s) {}
  float3 m_position{0.f, 0.f, 0.f};
  float3 m_direction{0.f, 0.f, -1.f};
  float m_intensity{1.f};
  float m_openingAngle{float(M_PI)};
  float m_falloffAngle{0.f};

  void commitParameters() override
  {
    Light::commitParameters();
    m_position = getParam<float3>("position", float3(0.f, 0.f, 0.f));
    m_direction = getParam<float3>("direction", float3(0.f, 0.f, -1.f));
    m_intensity = getParam<float>("intensity", getParam<float>("power", 1.f));
    m_openingAngle = getParam<float>("openingAngle", float(M_PI));
    m_falloffAngle = getParam<float>("falloffAngle", 0.f);
  }

  LightData buildLightData() const override
  {
    LightData d;
    d.type = HdPrimTypeTokens->sphereLight;
    d.xform = TranslateRotate(m_position, m_direction);
    // cone:angle is the half-angle in degrees; ANARI openingAngle is the full
    // cone angle in radians.
    const float coneDeg = 0.5f * m_openingAngle * 180.f / float(M_PI);
    const float softness =
        m_openingAngle > 0.f ? m_falloffAngle / m_openingAngle : 0.f;
    // spot = sphere light, treatAsPoint, with a shaping cone (SPEC §6.6).
    d.params = {{HdLightTokens->intensity,
                    VtValue(m_intensity * kPointIntensityScale)},
        {HdLightTokens->color, VtValue(ToVec3f(m_color))},
        {HdLightTokens->radius, VtValue(0.f)},
        {TfToken("treatAsPoint"), VtValue(true)},
        {HdLightTokens->shapingConeAngle, VtValue(coneDeg)},
        {HdLightTokens->shapingConeSoftness, VtValue(softness)}};
    return d;
  }
};

// quad -> rect light (SPEC §6.6): edge-vector parameterization.
struct Quad : public Light
{
  Quad(HydraGlobalState *s) : Light(s) {}
  float3 m_position{0.f, 0.f, 0.f};
  float3 m_edge1{1.f, 0.f, 0.f};
  float3 m_edge2{0.f, 1.f, 0.f};
  float m_intensity{1.f};
  bool m_normalize{false};

  void commitParameters() override
  {
    Light::commitParameters();
    m_position = getParam<float3>("position", float3(0.f, 0.f, 0.f));
    m_edge1 = getParam<float3>("edge1", float3(1.f, 0.f, 0.f));
    m_edge2 = getParam<float3>("edge2", float3(0.f, 1.f, 0.f));
    // power -> normalize=true (total emission), radiance -> normalize=false
    // (per-area), SPEC §6.7.
    if (hasParam("power")) {
      m_intensity = getParam<float>("power", 1.f);
      m_normalize = true;
    } else {
      m_intensity = getParam<float>("radiance", getParam<float>("intensity", 1.f));
      m_normalize = false;
    }
  }

  LightData buildLightData() const override
  {
    const GfVec3d e1(m_edge1[0], m_edge1[1], m_edge1[2]);
    const GfVec3d e2(m_edge2[0], m_edge2[1], m_edge2[2]);
    const double width = e1.GetLength();
    const double height = e2.GetLength();
    GfVec3d x = e1.GetNormalized();
    GfVec3d y = e2.GetNormalized();
    GfVec3d z = GfCross(x, y).GetNormalized();
    // Center of the quad; rect light emits from its local -Z face.
    GfVec3d center(m_position[0], m_position[1], m_position[2]);
    center += 0.5 * (e1 + e2);
    GfMatrix4d m(1);
    m.SetRow3(0, x);
    m.SetRow3(1, y);
    m.SetRow3(2, z);
    m.SetTranslateOnly(center);

    LightData d;
    d.type = HdPrimTypeTokens->rectLight;
    d.xform = m;
    d.params = {{HdLightTokens->intensity, VtValue(m_intensity)},
        {HdLightTokens->color, VtValue(ToVec3f(m_color))},
        {HdLightTokens->width, VtValue(float(width))},
        {HdLightTokens->height, VtValue(float(height))},
        {HdLightTokens->normalize, VtValue(m_normalize)}};
    return d;
  }
};

// ring -> disk light (SPEC §6.6): inner radius > 0 degrades to a disk (warned).
struct Ring : public Light
{
  Ring(HydraGlobalState *s) : Light(s) {}
  float3 m_position{0.f, 0.f, 0.f};
  float3 m_direction{0.f, 0.f, -1.f};
  float m_radius{1.f};
  float m_innerRadius{0.f};
  float m_intensity{1.f};
  bool m_normalize{false};

  void commitParameters() override
  {
    Light::commitParameters();
    m_position = getParam<float3>("position", float3(0.f, 0.f, 0.f));
    m_direction = getParam<float3>("direction", float3(0.f, 0.f, -1.f));
    m_radius = getParam<float>("radius", 1.f);
    m_innerRadius = getParam<float>("innerRadius", 0.f);
    // power -> normalize=true, radiance -> normalize=false (SPEC §6.7).
    if (hasParam("power")) {
      m_intensity = getParam<float>("power", 1.f);
      m_normalize = true;
    } else {
      m_intensity = getParam<float>("radiance", getParam<float>("intensity", 1.f));
      m_normalize = false;
    }
    if (m_innerRadius > 0.f) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "ring light innerRadius > 0 has no disk-light equivalent; "
          "degrading to a solid disk (SPEC §6.6)");
    }
  }

  LightData buildLightData() const override
  {
    LightData d;
    d.type = HdPrimTypeTokens->diskLight;
    d.xform = TranslateRotate(m_position, m_direction);
    d.params = {{HdLightTokens->intensity, VtValue(m_intensity)},
        {HdLightTokens->color, VtValue(ToVec3f(m_color))},
        {HdLightTokens->radius, VtValue(m_radius)},
        {HdLightTokens->normalize, VtValue(m_normalize)}};
    return d;
  }
};

// hdri -> dome light (SPEC §6.6). The `radiance` environment image rides the
// shared in-memory asset scheme (SPEC §6.3): it decodes into the AnariImage
// registry under this light's unique `anari://` path, and the dome light's
// textureFile points at it so Storm loads it through Glf->Hio->AnariHioImage —
// the same plugin the samplers use (HdEmbree reads it through Hio too, as
// linear float RGB). `scale` and `color` are folded into the served image and
// the dome's own intensity/color stay at 1: HdEmbree's dome ignores both, so
// only the texels reach every delegate alike. With no image, the
// constant-color dome (scale/color) stands.
struct HDRI : public Light
{
  HDRI(HydraGlobalState *s) : Light(s), m_radiance(this) {}
  ~HDRI() override
  {
    AnariImageRegistry::instance().erase(m_assetPath);
  }

  float m_scale{1.f};
  // Held through a ChangeObserverPtr so an in-place anariUnmapArray re-decodes
  // into the registry and re-marks the mirror (SPEC §4.5).
  helium::ChangeObserverPtr<helium::Array2D> m_radiance;
  std::string m_assetPath;
  uint64_t m_revision{0};
  bool m_haveImage{false};

  bool isHdri() const override { return true; }

  void commitParameters() override
  {
    Light::commitParameters();
    m_scale = getParam<float>("scale", 1.f);
    // Decode `radiance` (equirectangular ARRAY2D) into the registry at the
    // source's precision (TexelFormatFor): float/half sources keep HDR and
    // signed values, 8-bit sRGB stays an sRGB-typed 8-bit texture the GPU
    // decodes, 8-bit linear stays UNorm8.
    // A non-unit scale * color is a per-texel gain, which stores the image
    // as Float (TexelFormatFor). Every decode gets a fresh revisioned path:
    // delegates cache textures by path, so a scale edit (or an in-place
    // radiance edit) must change it to be picked up.
    m_radiance = getParamObject<helium::Array2D>("radiance");
    const float3 gain = m_color * m_scale;
    TexelAffine affine;
    affine.matrix = anari::math::mat4(anari::math::float4(gain.x, 0, 0, 0),
        anari::math::float4(0, gain.y, 0, 0),
        anari::math::float4(0, 0, gain.z, 0),
        anari::math::float4(0, 0, 0, 1));
    const bool unitGain = gain.x == 1.f && gain.y == 1.f && gain.z == 1.f;
    AnariImageData data;
    m_haveImage = DecodeArray2DToImage(
        m_radiance.get(), data, unitGain ? nullptr : &affine);
    data.environment = true;
    AnariImageRegistry::instance().erase(m_assetPath);
    m_assetPath = "anari://light/l" + std::to_string(serial()) + "_r"
        + std::to_string(++m_revision) + ".anari";
    if (m_haveImage)
      AnariImageRegistry::instance().set(m_assetPath, std::move(data));
  }

  LightData buildLightData() const override
  {
    LightData d;
    d.type = HdPrimTypeTokens->domeLight;
    // With an image, scale/color are already in its texels (commitParameters).
    d.params = {
        {HdLightTokens->intensity, VtValue(m_haveImage ? 1.f : m_scale)},
        {HdLightTokens->color,
            VtValue(m_haveImage ? GfVec3f(1.f, 1.f, 1.f) : ToVec3f(m_color))}};
    // With an environment image, author textureFile so the dome samples the real
    // texture instead of the constant color (SPEC §6.3/§6.6). Storm's dome-light
    // path reads SdfAssetPath::GetResolvedPath() directly (no Ar round-trip), so
    // set the resolved field too — the `anari://…​.anari` string dispatches Hio
    // to AnariHioImage by extension, needing no on-disk resolution.
    if (m_haveImage)
      d.params.emplace_back(HdLightTokens->textureFile,
          VtValue(SdfAssetPath(m_assetPath, m_assetPath)));
    return d;
  }
};

} // namespace

Light *Light::createInstance(std::string_view subtype, HydraGlobalState *s)
{
  if (subtype == "directional")
    return new Directional(s);
  else if (subtype == "point")
    return new Point(s);
  else if (subtype == "spot")
    return new Spot(s);
  else if (subtype == "quad")
    return new Quad(s);
  else if (subtype == "ring")
    return new Ring(s);
  else if (subtype == "hdri")
    return new HDRI(s);
  else
    return (Light *)new UnknownObject(ANARI_LIGHT, s);
}

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_DEFINITION(anari_hydra::Light *);
}
