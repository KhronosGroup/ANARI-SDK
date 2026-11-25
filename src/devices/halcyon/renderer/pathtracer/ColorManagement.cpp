// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: LicenseRef-TOST-1.0
//
// Ported from Typhoon (NVIDIA-Omniverse/OpenUSD typhoon/main at 70c45e83b,
// pxr/imaging/plugin/hdEmbree/renderer/colorManagement.cpp, Copyright 2026
// Pixar) and the parts of OpenUSD it calls (pxr/base/gf/colorSpace.cpp and
// pxr/base/gf/nc/nanocolor.c, Copyright 2024 Pixar: the colour space table,
// _InitColorSpace(), _ToLinear(), _FromLinear() and NcTransformColors() for
// one colour), all under the Tomorrow Open Source Technology License 1.0; see
// external/typhoon/LICENSE.txt. Only the linear Rec. 709 working space is
// kept; the arithmetic is nanocolor's, in its order, so results match
// OpenUSD's bitwise.

#include "ColorManagement.h"
// std
#include <cmath>
#include <cstring>

namespace halcyon::pathtracer {

const char *const WORKING_COLOR_SPACE = "lin_rec709_scene";

namespace {

// nanocolor's NcM33f: row-major
struct M33
{
  float m[9];
};

struct Chromaticity
{
  float x, y;
};

// nanocolor's NcColorSpace: the descriptor and what _InitColorSpace() derives
struct ColorSpace
{
  const char *descriptiveName;
  const char *shortName;
  Chromaticity red, green, blue, white;
  float gamma;
  float linearBias;
  float K0{0.f}, phi{0.f};
  M33 rgbToXYZ{{0, 0, 0, 0, 0, 0, 0, 0, 0}};
};

M33 invert(const M33 &m)
{
  M33 inv;
  const int M0 = 0, M1 = 3, M2 = 6, M3 = 1, M4 = 4, M5 = 7, M6 = 2, M7 = 5,
            M8 = 8;
  float det = m.m[M0] * (m.m[M4] * m.m[M8] - m.m[M5] * m.m[M7])
      - m.m[M1] * (m.m[M3] * m.m[M8] - m.m[M5] * m.m[M6])
      + m.m[M2] * (m.m[M3] * m.m[M7] - m.m[M4] * m.m[M6]);
  float invdet = 1.0 / det;
  inv.m[M0] = (m.m[M4] * m.m[M8] - m.m[M5] * m.m[M7]) * invdet;
  inv.m[M1] = (m.m[M2] * m.m[M7] - m.m[M1] * m.m[M8]) * invdet;
  inv.m[M2] = (m.m[M1] * m.m[M5] - m.m[M2] * m.m[M4]) * invdet;
  inv.m[M3] = (m.m[M5] * m.m[M6] - m.m[M3] * m.m[M8]) * invdet;
  inv.m[M4] = (m.m[M0] * m.m[M8] - m.m[M2] * m.m[M6]) * invdet;
  inv.m[M5] = (m.m[M2] * m.m[M3] - m.m[M0] * m.m[M5]) * invdet;
  inv.m[M6] = (m.m[M3] * m.m[M7] - m.m[M4] * m.m[M6]) * invdet;
  inv.m[M7] = (m.m[M1] * m.m[M6] - m.m[M0] * m.m[M7]) * invdet;
  inv.m[M8] = (m.m[M0] * m.m[M4] - m.m[M1] * m.m[M3]) * invdet;
  return inv;
}

M33 multiply(const M33 &lh, const M33 &rh)
{
  M33 m;
  m.m[0] = lh.m[0] * rh.m[0] + lh.m[1] * rh.m[3] + lh.m[2] * rh.m[6];
  m.m[1] = lh.m[0] * rh.m[1] + lh.m[1] * rh.m[4] + lh.m[2] * rh.m[7];
  m.m[2] = lh.m[0] * rh.m[2] + lh.m[1] * rh.m[5] + lh.m[2] * rh.m[8];
  m.m[3] = lh.m[3] * rh.m[0] + lh.m[4] * rh.m[3] + lh.m[5] * rh.m[6];
  m.m[4] = lh.m[3] * rh.m[1] + lh.m[4] * rh.m[4] + lh.m[5] * rh.m[7];
  m.m[5] = lh.m[3] * rh.m[2] + lh.m[4] * rh.m[5] + lh.m[5] * rh.m[8];
  m.m[6] = lh.m[6] * rh.m[0] + lh.m[7] * rh.m[3] + lh.m[8] * rh.m[6];
  m.m[7] = lh.m[6] * rh.m[1] + lh.m[7] * rh.m[4] + lh.m[8] * rh.m[7];
  m.m[8] = lh.m[6] * rh.m[2] + lh.m[7] * rh.m[5] + lh.m[8] * rh.m[8];
  return m;
}

// nanocolor's _InitColorSpace(), including its early return for the spaces
// whose matrix the table presets (which leaves their K0 and phi at 0)
void init(ColorSpace &cs)
{
  if (cs.rgbToXYZ.m[8] != 0.0)
    return;

  const float a = cs.linearBias;
  const float gamma = cs.gamma;

  if (gamma == 1.f) {
    cs.K0 = 1.e9f;
    cs.phi = 1.f;
  } else {
    if (a <= 0.f) {
      cs.K0 = 0.f;
      cs.phi = 1.f;
    } else {
      cs.K0 = a / (gamma - 1.f);
      cs.phi = (a / expf(logf(gamma * a / (gamma + gamma * a - 1.f - a)) * gamma))
          / (gamma - 1.f);
    }
  }

  if (cs.white.x == 0.f)
    return;

  // SMPTE RP 177-1993, as nanocolor
  float red[3] = {cs.red.x, cs.red.y, 1.f - cs.red.x - cs.red.y};
  float green[3] = {cs.green.x, cs.green.y, 1.f - cs.green.x - cs.green.y};
  float blue[3] = {cs.blue.x, cs.blue.y, 1.f - cs.blue.x - cs.blue.y};
  float white[3] = {cs.white.x, cs.white.y, 1.f - cs.white.x - cs.white.y};

  M33 m;
  m.m[0] = red[0];
  m.m[1] = green[0];
  m.m[2] = blue[0];
  m.m[3] = red[1];
  m.m[4] = green[1];
  m.m[5] = blue[1];
  m.m[6] = red[2];
  m.m[7] = green[2];
  m.m[8] = blue[2];

  float W[3] = {
      white[0] / white[1], white[1] / white[1], white[2] / white[1]};

  M33 mInv = invert(m);
  float C[3] = {mInv.m[0] * W[0] + mInv.m[1] * W[1] + mInv.m[2] * W[2],
      mInv.m[3] * W[0] + mInv.m[4] * W[1] + mInv.m[5] * W[2],
      mInv.m[6] * W[0] + mInv.m[7] * W[1] + mInv.m[8] * W[2]};

  m.m[0] *= C[0];
  m.m[1] *= C[1];
  m.m[2] *= C[2];
  m.m[3] *= C[0];
  m.m[4] *= C[1];
  m.m[5] *= C[2];
  m.m[6] *= C[0];
  m.m[7] *= C[1];
  m.m[8] *= C[2];

  cs.rgbToXYZ = m;
}

// nanocolor's _colorSpaces, in its order
struct ColorSpaceTable
{
  static constexpr int COUNT = 18;
  ColorSpace spaces[COUNT];

  ColorSpaceTable()
  {
    const Chromaticity ap1R{0.71319588766205f, 0.29268891446333f};
    const Chromaticity ap1G{0.15950855654178f, 0.83878851615096f};
    const Chromaticity ap1B{0.128672995285350f, 0.043895571160528f};
    const Chromaticity rec709R{0.640f, 0.330f};
    const Chromaticity rec709G{0.300f, 0.600f};
    const Chromaticity rec709B{0.150f, 0.060f};
    const Chromaticity p3R{0.6800f, 0.3200f};
    const Chromaticity p3G{0.2650f, 0.6900f};
    const Chromaticity p3B{0.1500f, 0.0600f};
    const Chromaticity adobeR{0.64f, 0.33f};
    const Chromaticity adobeG{0.21f, 0.71f};
    const Chromaticity adobeB{0.15f, 0.06f};
    const Chromaticity d65{0.3127f, 0.3290f};
    // these chromaticities generate identity; the matrix is preset
    const Chromaticity idR{1.f, 0.f};
    const Chromaticity idG{0.f, 1.f};
    const Chromaticity idB{0.f, 0.f};
    const Chromaticity idW{float(1.0 / 3.0), float(1.0 / 3.0)};
    const M33 identity{{1, 0, 0, 0, 1, 0, 0, 0, 1}};

    int i = 0;
    auto add = [&](const char *descriptive,
                   const char *name,
                   Chromaticity r,
                   Chromaticity g,
                   Chromaticity b,
                   Chromaticity w,
                   float gamma,
                   float bias,
                   bool presetIdentity = false) {
      ColorSpace &cs = spaces[i++];
      cs.descriptiveName = descriptive;
      cs.shortName = name;
      cs.red = r;
      cs.green = g;
      cs.blue = b;
      cs.white = w;
      cs.gamma = gamma;
      cs.linearBias = bias;
      if (presetIdentity)
        cs.rgbToXYZ = identity;
      init(cs);
    };

    // These primaries are preadapted to D65 using a Bradford transform
    add("ACEScg", "lin_ap1_scene", ap1R, ap1G, ap1B, d65, 1.0f, 0.0f);
    add("ACES2065-1",
        "lin_ap0_scene",
        {0.73485524337371f, 0.26422532524554f},
        {-0.0061709124786224f, 1.0113149590212864f},
        {0.015967559255041f, -0.064235503128551f},
        d65,
        1.0f,
        0.0f);
    add("Linear Rec.709 (sRGB)",
        "lin_rec709_scene",
        rec709R,
        rec709G,
        rec709B,
        d65,
        1.0f,
        0.0f);
    add("Linear P3-D65", "lin_p3d65_scene", p3R, p3G, p3B, d65, 1.0f, 0.0f);
    add("Linear Rec.2020",
        "lin_rec2020_scene",
        {0.708f, 0.292f},
        {0.170f, 0.797f},
        {0.131f, 0.046f},
        d65,
        1.0f,
        0.0f);
    add("Linear AdobeRGB",
        "lin_adobergb_scene",
        adobeR,
        adobeG,
        adobeB,
        d65,
        1.0f,
        0.0f);
    add("CIE XYZ-D65 - Scene-referred",
        "lin_ciexyzd65_scene",
        {1.0f, 0.0f},
        {0.0f, 1.0f},
        {0.0f, 0.0f},
        d65,
        1.0f,
        0.0f);
    add("sRGB Encoded Rec.709 (sRGB)",
        "srgb_rec709_scene",
        rec709R,
        rec709G,
        rec709B,
        d65,
        2.4f,
        0.055f);
    add("Gamma 2.2 Encoded Rec.709",
        "g22_rec709_scene",
        rec709R,
        rec709G,
        rec709B,
        d65,
        2.2f,
        0.0f);
    add("Gamma 1.8 Encoded Rec.709",
        "g18_rec709_scene",
        rec709R,
        rec709G,
        rec709B,
        d65,
        1.8f,
        0.0f);
    add("sRGB Encoded AP1", "srgb_ap1_scene", ap1R, ap1G, ap1B, d65, 2.4f, 0.055f);
    add("Gamma 2.2 Encoded AP1",
        "g22_ap1_scene",
        ap1R,
        ap1G,
        ap1B,
        d65,
        2.2f,
        0.0f);
    add("sRGB Encoded P3-D65",
        "srgb_p3d65_scene",
        p3R,
        p3G,
        p3B,
        d65,
        2.4f,
        0.055f);
    add("Gamma 2.2 Encoded AdobeRGB",
        "g22_adobergb_scene",
        adobeR,
        adobeG,
        adobeB,
        d65,
        2.2f,
        0.0f);
    add("Data", "data", idR, idG, idB, idW, 1.0f, 0.0f, true);
    add("Identity", "identity", idR, idG, idB, idW, 1.0f, 0.0f, true);
    add("Raw", "raw", idR, idG, idB, idW, 1.0f, 0.0f, true);
    add("Unknown", "unknown", idR, idG, idB, idW, 1.0f, 0.0f, true);
  }

  // NcGetNamedColorSpace(): by short or descriptive name
  const ColorSpace *find(const std::string &name) const
  {
    for (const ColorSpace &cs : spaces) {
      if (name == cs.shortName || name == cs.descriptiveName)
        return &cs;
    }
    return nullptr;
  }
};

const ColorSpaceTable &table()
{
  static const ColorSpaceTable t;
  return t;
}

float toLinear(const ColorSpace &cs, float t)
{
  if (t < cs.K0)
    return t / cs.phi;
  const float a = cs.linearBias;
  return powf((t + a) / (1.f + a), cs.gamma);
}

float fromLinear(const ColorSpace &cs, float t)
{
  if (t < cs.K0 / cs.phi)
    return t * cs.phi;
  const float a = cs.linearBias;
  return (1.f + a) * powf(t, 1.f / cs.gamma) - a;
}

// NcTransformColors() for one colour (its scalar path)
mxcpp::Vec3f transform(
    const ColorSpace &dst, const ColorSpace &src, const mxcpp::Vec3f &rgb)
{
  const M33 tx = multiply(invert(dst.rgbToXYZ), src.rgbToXYZ);
  const float r = toLinear(src, rgb[0]);
  const float g = toLinear(src, rgb[1]);
  const float b = toLinear(src, rgb[2]);
  mxcpp::Vec3f out(tx.m[0] * r + tx.m[1] * g + tx.m[2] * b,
      tx.m[3] * r + tx.m[4] * g + tx.m[5] * b,
      tx.m[6] * r + tx.m[7] * g + tx.m[8] * b);
  out[0] = fromLinear(dst, out[0]);
  out[1] = fromLinear(dst, out[1]);
  out[2] = fromLinear(dst, out[2]);
  return out;
}

} // namespace

bool convertToWorkingSpace(const std::string &source, mxcpp::Vec3f &rgb)
{
  // Typhoon's ResolveColorSpace(): these names mean no transform
  if (source == "data" || source == "raw" || source == "identity"
      || source == "unknown")
    return true;
  const ColorSpace *src = table().find(source);
  if (!src)
    return false;
  if (source == WORKING_COLOR_SPACE)
    return true;
  rgb = transform(*table().find(WORKING_COLOR_SPACE), *src, rgb);
  return true;
}

mxcpp::Vec3f luminanceCoefficients()
{
  const M33 &m = table().find(WORKING_COLOR_SPACE)->rgbToXYZ;
  return mxcpp::Vec3f(m.m[3], m.m[4], m.m[5]);
}

bool colorTransform(const void *,
    const std::string &source,
    const std::string &target,
    const mxcpp::Vec3f &in,
    mxcpp::Vec3f *out)
{
  if (!out || target != WORKING_COLOR_SPACE)
    return false;
  mxcpp::Vec3f rgb = in;
  if (!convertToWorkingSpace(source, rgb))
    return false;
  *out = rgb;
  return true;
}

} // namespace halcyon::pathtracer
