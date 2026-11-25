// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises sampler coordinate selection and transforms: two samplers bound to
// different attributes on one surface, inTransform rotation / shear / offsets
// (including a translation carried in the matrix's fourth column), outTransform
// channel swizzles and mixing, nearest vs linear interpolation through a
// transform, and live edits of all of the above.
//
// A quad carries three UV-shaped attributes: vertex.attribute0 = (u,v),
// vertex.attribute1 = (1-u, v) and vertex.color = (u, v, 0, 1). A 2x2 nearest
// checkerboard (R,G / B,W) is sampled onto it and each quadrant is classified
// against the texel it must show; a second 2x1 sampler drives opacity so the
// frame's alpha checks the second binding independently.

#include <anari/anari.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_errors = 0;
static bool g_faceVarying = false;

static void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  const char *tag = severity == ANARI_SEVERITY_FATAL_ERROR ? "FATAL"
      : severity == ANARI_SEVERITY_ERROR                   ? "ERROR"
      : severity == ANARI_SEVERITY_WARNING                 ? "WARN"
                                                           : "INFO";
  if (severity <= ANARI_SEVERITY_WARNING)
    std::printf("[%s] %s\n", tag, message);
  if (severity <= ANARI_SEVERITY_ERROR)
    ++g_errors;
}

static void check(bool ok, const std::string &what)
{
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok)
    ++g_errors;
}

struct Img
{
  uint32_t w = 0, h = 0;
  std::vector<uint8_t> rgba;
  const uint8_t *at(int x, int y) const
  {
    return &rgba[(size_t(y) * w + x) * 4];
  }
};

// Classify a color by channel ratios (robust to shading and alpha blending):
// R/G/B/W for the checker texels, K for black/uncovered, ? for a blend.
static char classify(double r, double g, double b)
{
  const double m = std::max(r, std::max(g, b));
  if (m < 30)
    return 'K';
  if (std::min(r, std::min(g, b)) > 0.8 * m)
    return 'W';
  if (r == m && g < 0.35 * m && b < 0.35 * m)
    return 'R';
  if (g == m && r < 0.35 * m && b < 0.35 * m)
    return 'G';
  if (b == m && r < 0.35 * m && g < 0.35 * m)
    return 'B';
  return '?';
}

static char classifyPixel(const uint8_t *p)
{
  return classify(p[0], p[1], p[2]);
}

// Average color over a window (the alpha average is taken over all pixels).
static void windowAvg(const Img &im, int cx, int cy, int r, double out[4])
{
  double s[4] = {0, 0, 0, 0};
  long n = 0;
  for (int y = cy - r; y <= cy + r; ++y)
    for (int x = cx - r; x <= cx + r; ++x) {
      if (x < 0 || y < 0 || x >= int(im.w) || y >= int(im.h))
        continue;
      const uint8_t *p = im.at(x, y);
      for (int k = 0; k < 4; ++k)
        s[k] += p[k];
      ++n;
    }
  for (int k = 0; k < 4; ++k)
    out[k] = n ? s[k] / n : 0;
}

static int countBlended(const Img &im, int y, int x0, int x1)
{
  int blended = 0;
  for (int x = x0; x <= x1; ++x)
    if (classifyPixel(im.at(x, y)) == '?')
      ++blended;
  return blended;
}

struct Rig
{
  ANARIDevice dev{};
  ANARIArray2D checker{}, opacityImg{};
  ANARISampler color{}, opacity{};
  ANARIMaterial mat{};
  ANARIArray1D pos{}, attr0{}, attr1{}, colorAttr{}, index{};
  ANARIGeometry g{};
  ANARISurface surf{};
  ANARIArray1D surfArray{};
  ANARIWorld world{};
  ANARICamera cam{};
  ANARIRenderer ren{};
  ANARIFrame frame{};

  // Attribute storage: ANARI arrays wrap app memory zero-copy, so it must
  // outlive the arrays.
  std::vector<float> attrData[3];

  // Quadrant probe geometry, from the baseline render.
  int mx = 0, my = 0, qx = 0, qy = 0, r = 2, x0 = 0, x1 = 0;
};

static void setMat4(
    ANARIDevice dev, ANARIObject o, const char *name, const float m[16])
{
  anariSetParameter(dev, o, name, ANARI_FLOAT32_MAT4, m);
}

static void setVec4(ANARIDevice dev,
    ANARIObject o,
    const char *name,
    float x,
    float y,
    float z,
    float w)
{
  const float v[4] = {x, y, z, w};
  anariSetParameter(dev, o, name, ANARI_FLOAT32_VEC4, v);
}

static void setStr(
    ANARIDevice dev, ANARIObject o, const char *name, const char *v)
{
  anariSetParameter(dev, o, name, ANARI_STRING, v);
}

static ANARIArray1D makeAttr(ANARIDevice dev,
    std::vector<float> &data,
    const float *perVertex,
    int width)
{
  // Two triangles (0,1,2)(0,2,3) share positions but carry six UV corners in
  // face-varying mode; otherwise the four vertices carry the UVs directly.
  static const int corners[6] = {0, 1, 2, 0, 2, 3};
  data.clear();
  const int n = g_faceVarying ? 6 : 4;
  for (int i = 0; i < n; ++i) {
    const int v = g_faceVarying ? corners[i] : i;
    for (int k = 0; k < width; ++k)
      data.push_back(perVertex[v * width + k]);
  }
  const ANARIDataType type =
      width == 4 ? ANARI_FLOAT32_VEC4 : ANARI_FLOAT32_VEC2;
  return anariNewArray1D(dev, data.data(), nullptr, nullptr, type, n);
}

static void buildRig(Rig &rc)
{
  ANARIDevice dev = rc.dev;

  // 2x2 checkerboard: row 0 (v=0) = R, G; row 1 (v=1) = B, W.
  static const uint8_t checker[2 * 2 * 4] = {
      255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
  rc.checker =
      anariNewArray2D(dev, checker, nullptr, nullptr, ANARI_UFIXED8_VEC4, 2, 2);
  rc.color = anariNewSampler(dev, "image2D");
  anariSetParameter(dev, rc.color, "image", ANARI_ARRAY2D, &rc.checker);
  setStr(dev, rc.color, "filter", "nearest");
  anariCommitParameters(dev, rc.color);

  // 2x1 opacity image: texel 0 = (1, 1, 0), texel 1 = (0.5, 0, 0). The scalar
  // slot reads red by default -> left opaque, right half-transparent.
  static const uint8_t packed[2 * 4] = {255, 255, 0, 255, 128, 0, 0, 255};
  rc.opacityImg =
      anariNewArray2D(dev, packed, nullptr, nullptr, ANARI_UFIXED8_VEC4, 2, 1);
  rc.opacity = anariNewSampler(dev, "image2D");
  anariSetParameter(dev, rc.opacity, "image", ANARI_ARRAY2D, &rc.opacityImg);
  setStr(dev, rc.opacity, "filter", "nearest");
  anariCommitParameters(dev, rc.opacity);

  rc.mat = anariNewMaterial(dev, "physicallyBased");
  anariSetParameter(dev, rc.mat, "baseColor", ANARI_SAMPLER, &rc.color);
  anariSetParameter(dev, rc.mat, "opacity", ANARI_SAMPLER, &rc.opacity);
  setStr(dev, rc.mat, "alphaMode", "blend");
  anariCommitParameters(dev, rc.mat);

  static const float verts[12] = {
      -0.9f, -0.9f, 0.f, 0.9f, -0.9f, 0.f, 0.9f, 0.9f, 0.f, -0.9f, 0.9f, 0.f};
  static const float st[8] = {0, 0, 1, 0, 1, 1, 0, 1};
  static const float mirrored[8] = {1, 0, 0, 0, 0, 1, 1, 1}; // (1-u, v)
  static const float colorUv[16] = {
      0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1};
  rc.pos = anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  rc.attr0 = makeAttr(dev, rc.attrData[0], st, 2);
  rc.attr1 = makeAttr(dev, rc.attrData[1], mirrored, 2);
  rc.colorAttr = makeAttr(dev, rc.attrData[2], colorUv, 4);
  rc.g = anariNewGeometry(dev, g_faceVarying ? "triangle" : "quad");
  anariSetParameter(dev, rc.g, "vertex.position", ANARI_ARRAY1D, &rc.pos);
  const char *prefix = g_faceVarying ? "faceVarying." : "vertex.";
  anariSetParameter(dev,
      rc.g,
      (std::string(prefix) + "attribute0").c_str(),
      ANARI_ARRAY1D,
      &rc.attr0);
  anariSetParameter(dev,
      rc.g,
      (std::string(prefix) + "attribute1").c_str(),
      ANARI_ARRAY1D,
      &rc.attr1);
  anariSetParameter(dev,
      rc.g,
      (std::string(prefix) + "color").c_str(),
      ANARI_ARRAY1D,
      &rc.colorAttr);
  if (g_faceVarying) {
    static const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    rc.index =
        anariNewArray1D(dev, indices, nullptr, nullptr, ANARI_UINT32_VEC3, 2);
    anariSetParameter(dev, rc.g, "primitive.index", ANARI_ARRAY1D, &rc.index);
  }
  anariCommitParameters(dev, rc.g);

  rc.surf = anariNewSurface(dev);
  anariSetParameter(dev, rc.surf, "geometry", ANARI_GEOMETRY, &rc.g);
  anariSetParameter(dev, rc.surf, "material", ANARI_MATERIAL, &rc.mat);
  anariCommitParameters(dev, rc.surf);

  rc.world = anariNewWorld(dev);
  rc.surfArray =
      anariNewArray1D(dev, &rc.surf, nullptr, nullptr, ANARI_SURFACE, 1);
  anariSetParameter(dev, rc.world, "surface", ANARI_ARRAY1D, &rc.surfArray);
  anariCommitParameters(dev, rc.world);

  rc.cam = anariNewCamera(dev, "perspective");
  const float eye[3] = {0.f, 0.f, 3.f};
  const float dir[3] = {0.f, 0.f, -1.f};
  const float up[3] = {0.f, 1.f, 0.f};
  const float fovy = 0.6f;
  anariSetParameter(dev, rc.cam, "position", ANARI_FLOAT32_VEC3, eye);
  anariSetParameter(dev, rc.cam, "direction", ANARI_FLOAT32_VEC3, dir);
  anariSetParameter(dev, rc.cam, "up", ANARI_FLOAT32_VEC3, up);
  anariSetParameter(dev, rc.cam, "fovy", ANARI_FLOAT32, &fovy);
  anariCommitParameters(dev, rc.cam);

  rc.ren = anariNewRenderer(dev, "default");
  const float background[4] = {0, 0, 0, 0};
  anariSetParameter(dev, rc.ren, "background", ANARI_FLOAT32_VEC4, background);
  anariCommitParameters(dev, rc.ren);

  rc.frame = anariNewFrame(dev);
  const uint32_t size[2] = {256, 256};
  const ANARIDataType colorFmt = ANARI_UFIXED8_RGBA_SRGB;
  anariSetParameter(dev, rc.frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(dev, rc.frame, "channel.color", ANARI_DATA_TYPE, &colorFmt);
  anariSetParameter(dev, rc.frame, "world", ANARI_WORLD, &rc.world);
  anariSetParameter(dev, rc.frame, "camera", ANARI_CAMERA, &rc.cam);
  anariSetParameter(dev, rc.frame, "renderer", ANARI_RENDERER, &rc.ren);
  anariCommitParameters(dev, rc.frame);
}

static void releaseRig(Rig &rc)
{
  ANARIDevice dev = rc.dev;
  for (ANARIObject o : {(ANARIObject)rc.frame,
           (ANARIObject)rc.ren,
           (ANARIObject)rc.cam,
           (ANARIObject)rc.surfArray,
           (ANARIObject)rc.world,
           (ANARIObject)rc.surf,
           (ANARIObject)rc.g,
           (ANARIObject)rc.index,
           (ANARIObject)rc.colorAttr,
           (ANARIObject)rc.attr1,
           (ANARIObject)rc.attr0,
           (ANARIObject)rc.pos,
           (ANARIObject)rc.mat,
           (ANARIObject)rc.opacity,
           (ANARIObject)rc.color,
           (ANARIObject)rc.opacityImg,
           (ANARIObject)rc.checker})
    if (o)
      anariRelease(dev, o);
}

static void render(Rig &rc, Img &im)
{
  anariRenderFrame(rc.dev, rc.frame);
  anariFrameReady(rc.dev, rc.frame, ANARI_WAIT);
  ANARIDataType pt = ANARI_UNKNOWN;
  const uint8_t *pixels = (const uint8_t *)anariMapFrame(
      rc.dev, rc.frame, "channel.color", &im.w, &im.h, &pt);
  if (pixels)
    im.rgba.assign(pixels, pixels + size_t(im.w) * im.h * 4);
  anariUnmapFrame(rc.dev, rc.frame, "channel.color");
}

// Classify the four quadrant centers of the quad as "BL BR TL TR" (frame rows
// run bottom-up, so +y is the top / v=1 side of the quad).
static std::string quadrants(const Rig &rc, const Img &im)
{
  const int px[4] = {
      rc.mx - rc.qx, rc.mx + rc.qx, rc.mx - rc.qx, rc.mx + rc.qx};
  const int py[4] = {
      rc.my - rc.qy, rc.my - rc.qy, rc.my + rc.qy, rc.my + rc.qy};
  std::string out;
  for (int i = 0; i < 4; ++i) {
    double c[4];
    windowAvg(im, px[i], py[i], rc.r, c);
    out += classify(c[0], c[1], c[2]);
  }
  return out;
}

static void expectQuadrants(Rig &rc, const char *expected, const char *what)
{
  Img im;
  render(rc, im);
  const std::string got = quadrants(rc, im);
  check(got == expected,
      std::string(what) + " -> quadrants " + got + " (expected " + expected
          + ")");
}

// Alpha averaged at the left / right half centers.
static void expectAlpha(Rig &rc, int left, int right, const char *what)
{
  Img im;
  render(rc, im);
  double l[4], r[4];
  windowAvg(im, rc.mx - rc.qx, rc.my, rc.r, l);
  windowAvg(im, rc.mx + rc.qx, rc.my, rc.r, r);
  const bool ok = std::fabs(l[3] - left) <= 3 && std::fabs(r[3] - right) <= 3;
  char buf[160];
  std::snprintf(buf,
      sizeof buf,
      "%s -> alpha L=%.0f R=%.0f (expected %d %d)",
      what,
      l[3],
      r[3],
      left,
      right);
  check(ok, buf);
}

// Blended-pixel count along the bottom-row scanline (crosses a texel boundary
// in every case exercised).
static int bottomRowBlend(Rig &rc)
{
  Img im;
  render(rc, im);
  return countBlended(im, rc.my - rc.qy, rc.x0, rc.x1);
}

// Column-major float[16] (ANARI_FLOAT32_MAT4): index = column*4 + row.
struct Mat4
{
  float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  Mat4 &set(int column, int row, float v)
  {
    m[column * 4 + row] = v;
    return *this;
  }
};

int main(int argc, char **argv)
{
  const char *subtype = argc > 1 ? argv[1] : "storm";
  g_faceVarying = argc > 2 && std::strcmp(argv[2], "faceVarying") == 0;

  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  if (!lib)
    return 2;
  ANARIDevice dev = anariNewDevice(lib, subtype);
  if (!dev)
    return 2;

  Rig rc;
  rc.dev = dev;
  buildRig(rc);

  // --- Baseline: covered bbox -> quadrant probes.
  Img base;
  render(rc, base);
  int x0 = base.w, y0 = base.h, x1 = 0, y1 = 0;
  for (uint32_t y = 0; y < base.h; ++y)
    for (uint32_t x = 0; x < base.w; ++x) {
      const uint8_t *p = base.at(x, y);
      if (p[0] > 20 || p[1] > 20 || p[2] > 20) {
        x0 = std::min<int>(x0, x);
        y0 = std::min<int>(y0, y);
        x1 = std::max<int>(x1, x);
        y1 = std::max<int>(y1, y);
      }
    }
  check(x1 > x0 && y1 > y0, "textured quad is visible");
  rc.mx = (x0 + x1) / 2;
  rc.my = (y0 + y1) / 2;
  rc.qx = (x1 - x0) / 4;
  rc.qy = (y1 - y0) / 4;
  rc.r = std::max(2, std::min(rc.qx, rc.qy) / 2);
  rc.x0 = x0;
  rc.x1 = x1;
  std::printf("  bbox %d %d %d %d  probes m=(%d,%d) q=(%d,%d) r=%d\n",
      x0,
      y0,
      x1,
      y1,
      rc.mx,
      rc.my,
      rc.qx,
      rc.qy,
      rc.r);
  check(quadrants(rc, base) == "RGBW", "baseline attribute0 checker RGBW");
  expectAlpha(rc, 255, 128, "second sampler drives opacity from attribute0");

  // --- Coordinate selection (live edits of inAttribute on the color sampler).
  setStr(dev, rc.color, "inAttribute", "attribute1");
  anariCommitParameters(dev, rc.color);
  expectQuadrants(rc, "GRWB", "inAttribute=attribute1 mirrors U");
  expectAlpha(rc, 255, 128, "opacity sampler keeps attribute0");

  setStr(dev, rc.color, "inAttribute", "color");
  anariCommitParameters(dev, rc.color);
  expectQuadrants(rc, "RGBW", "inAttribute=color reads vertex.color.xy");

  anariUnsetParameter(dev, rc.color, "inAttribute");
  anariCommitParameters(dev, rc.color);
  expectQuadrants(rc, "RGBW", "unsetting inAttribute restores attribute0");

  // --- An unknown inAttribute name reads as an absent attribute: the same
  //     frame as attribute2, which this rig never authors (reader fallback
  //     (0,0) -> one texel everywhere), and not the attribute0 checker. Checked
  //     on the UsdTransform2d path and, under a shear, on the baked path. (The
  //     position/normal/primitiveId sources are real sources, exercised in
  //     render_sampler_derived.)
  {
    setStr(dev, rc.color, "inAttribute", "attribute2");
    anariCommitParameters(dev, rc.color);
    Img absentImg;
    render(rc, absentImg);
    const std::string absent = quadrants(rc, absentImg);
    std::printf("  absent attribute2 quadrants: %s\n", absent.c_str());
    check(absent != "RGBW", "absent attribute2 does not read attribute0");
    check(absent.find_first_not_of(absent[0]) == std::string::npos,
        "absent attribute2 samples one texel everywhere");
    setStr(dev, rc.color, "inAttribute", "notAnAttribute");
    anariCommitParameters(dev, rc.color);
    expectQuadrants(
        rc, absent.c_str(), "unknown inAttribute reads as an absent attribute");

    Mat4 shearAbsent;
    shearAbsent.set(1, 0, 0.5f);
    setMat4(dev, rc.color, "inTransform", shearAbsent.m);
    setStr(dev, rc.color, "inAttribute", "attribute2");
    anariCommitParameters(dev, rc.color);
    Img absentBakedImg;
    render(rc, absentBakedImg);
    const std::string absentBaked = quadrants(rc, absentBakedImg);
    check(absentBaked != "RGWW",
        "absent attribute2 under shear does not read attribute0");
    setStr(dev, rc.color, "inAttribute", "notAnAttribute");
    anariCommitParameters(dev, rc.color);
    expectQuadrants(rc,
        absentBaked.c_str(),
        "baked unknown inAttribute reads as an absent attribute");

    anariUnsetParameter(dev, rc.color, "inTransform");
    anariUnsetParameter(dev, rc.color, "inAttribute");
    anariCommitParameters(dev, rc.color);
    expectQuadrants(rc, "RGBW", "restoring attribute0 after absent sources");
  }

  // --- Rotation by 90 degrees about the texture center: t' = (1-v, u).
  //     Column 0 = (0,1), column 1 = (-1,0), translation in inOffset.
  Mat4 rot90;
  rot90.set(0, 0, 0).set(0, 1, 1).set(1, 0, -1).set(1, 1, 0);
  setMat4(dev, rc.color, "inTransform", rot90.m);
  setVec4(dev, rc.color, "inOffset", 1, 0, 5, 7);
  anariCommitParameters(dev, rc.color);
  expectQuadrants(rc, "GWRB", "inTransform rotation 90 + inOffset");
  const int rotNearest = bottomRowBlend(rc);
  setStr(dev, rc.color, "filter", "linear");
  anariCommitParameters(dev, rc.color);
  expectQuadrants(rc, "GWRB", "rotation under linear filtering");
  const int rotLinear = bottomRowBlend(rc);
  std::printf(
      "  rotation blend px  nearest=%d  linear=%d\n", rotNearest, rotLinear);
  check(rotNearest < 4, "rotation + nearest snaps at the texel boundary");
  check(rotLinear > 10, "rotation + linear interpolates across the boundary");
  setStr(dev, rc.color, "filter", "nearest");

  // --- Same rotation with the translation in the matrix's fourth column.
  Mat4 rot90w = rot90;
  rot90w.set(3, 0, 1);
  setMat4(dev, rc.color, "inTransform", rot90w.m);
  anariUnsetParameter(dev, rc.color, "inOffset");
  anariCommitParameters(dev, rc.color);
  expectQuadrants(rc, "GWRB", "rotation with translation in column 3 (w=1)");

  // --- Shear: t' = (u + 0.5 v, v). Clamp puts TR past u=1 onto W.
  Mat4 shear;
  shear.set(1, 0, 0.5f);
  setMat4(dev, rc.color, "inTransform", shear.m);
  anariCommitParameters(dev, rc.color);
  expectQuadrants(rc, "RGWW", "inTransform shear");
  const int shearNearest = bottomRowBlend(rc);
  // (No quadrant check under linear: the sheared BL probe lands at u'=0.375,
  // inside the texel-center span, so a blend is the correct answer there.)
  setStr(dev, rc.color, "filter", "linear");
  anariCommitParameters(dev, rc.color);
  const int shearLinear = bottomRowBlend(rc);
  std::printf(
      "  shear blend px  nearest=%d  linear=%d\n", shearNearest, shearLinear);
  check(shearNearest < 4, "shear + nearest snaps at the texel boundary");
  check(shearLinear > 10, "shear + linear interpolates across the boundary");
  setStr(dev, rc.color, "filter", "nearest");

  // --- Baked path on other attributes. attribute1 = (1-u, v) sheared:
  //     t' = (1-u + 0.5v, v): BL->(.875,.25) G; BR->(.375,.25) R; TL->clamp W;
  //     TR->(.625,.75) W. vertex.color = (u,v,0,1) sheared matches attribute0.
  setStr(dev, rc.color, "filter", "nearest");
  setStr(dev, rc.color, "inAttribute", "attribute1");
  anariCommitParameters(dev, rc.color);
  expectQuadrants(rc, "GRWW", "shear baked on attribute1");
  setStr(dev, rc.color, "inAttribute", "color");
  anariCommitParameters(dev, rc.color);
  expectQuadrants(rc, "RGWW", "shear baked on vertex.color");
  anariUnsetParameter(dev, rc.color, "inAttribute");
  anariCommitParameters(dev, rc.color);

  // --- z-column terms see the ANARI padding z=0 of a 2-component attribute.
  Mat4 zjunk;
  zjunk.set(2, 0, 3).set(2, 1, 3);
  setMat4(dev, rc.color, "inTransform", zjunk.m);
  anariCommitParameters(dev, rc.color);
  expectQuadrants(rc, "RGBW", "z-column terms multiply the padded z=0");

  // --- Reflection + non-uniform scale (rotation/scale decomposable):
  //     t' = (1 - u, 2v - 0.5): BL (.25,.25)->(.75, 0) G; BR->(.25,0) R;
  //     TL (.25,.75)->(.75,1) W; TR->(.25,1) B.
  Mat4 reflect;
  reflect.set(0, 0, -1).set(1, 1, 2);
  setMat4(dev, rc.color, "inTransform", reflect.m);
  setVec4(dev, rc.color, "inOffset", 1, -0.5f, 0, 0);
  anariCommitParameters(dev, rc.color);
  expectQuadrants(rc, "GRWB", "reflection + non-uniform scale + offset");

  // --- Two samplers with independent transforms and attributes.
  setStr(dev, rc.color, "inAttribute", "attribute1");
  setMat4(dev, rc.color, "inTransform", rot90.m);
  setVec4(dev, rc.color, "inOffset", 1, 0, 0, 0);
  anariCommitParameters(dev, rc.color);
  Mat4 mirrorU;
  mirrorU.set(0, 0, -1);
  setMat4(dev, rc.opacity, "inTransform", mirrorU.m);
  setVec4(dev, rc.opacity, "inOffset", 1, 0, 0, 0);
  anariCommitParameters(dev, rc.opacity);
  expectQuadrants(rc, "WGBR", "color: attribute1 + rotation");
  expectAlpha(rc, 128, 255, "opacity: attribute0 + mirrored U");

  anariUnsetParameter(dev, rc.color, "inAttribute");
  anariUnsetParameter(dev, rc.color, "inTransform");
  anariUnsetParameter(dev, rc.color, "inOffset");
  anariCommitParameters(dev, rc.color);
  anariUnsetParameter(dev, rc.opacity, "inTransform");
  anariUnsetParameter(dev, rc.opacity, "inOffset");
  anariCommitParameters(dev, rc.opacity);
  expectQuadrants(rc, "RGBW", "unsetting input transforms restores identity");
  expectAlpha(rc, 255, 128, "opacity restored");

  // --- Output swizzle: swap R and B (out.r = in.b, out.b = in.r).
  Mat4 swapRB;
  swapRB.set(0, 0, 0).set(0, 2, 1).set(2, 2, 0).set(2, 0, 1);
  setMat4(dev, rc.color, "outTransform", swapRB.m);
  anariCommitParameters(dev, rc.color);
  expectQuadrants(rc, "BGRW", "outTransform swaps R and B");

  // --- Output mixing: out = (0.5 r + 0.5 g, 0, 0) + (0, 0, 0, 0).
  Mat4 mixRG;
  for (int c = 0; c < 3; ++c)
    for (int r = 0; r < 3; ++r)
      mixRG.set(c, r, 0);
  mixRG.set(0, 0, 0.5f).set(1, 0, 0.5f);
  setMat4(dev, rc.color, "outTransform", mixRG.m);
  anariCommitParameters(dev, rc.color);
  expectQuadrants(rc, "RRKR", "outTransform mixes R and G into red");

  // --- Output mixing with an offset feeding a different channel.
  setVec4(dev, rc.color, "outOffset", 0, 0, 1, 0);
  anariCommitParameters(dev, rc.color);
  {
    Img im;
    render(rc, im);
    double c[4];
    windowAvg(im, rc.mx - rc.qx, rc.my + rc.qy, rc.r, c); // B texel -> (0,0,1)
    check(classify(c[0], c[1], c[2]) == 'B',
        "outOffset adds blue where the mixed red is zero");
    windowAvg(im, rc.mx + rc.qx, rc.my + rc.qy, rc.r, c); // W texel -> (1,0,1)
    // Magenta (this half of the quad is alpha-blended at 0.5, hence dimmer).
    std::printf("  mixed+offset TR = %.0f %.0f %.0f\n", c[0], c[1], c[2]);
    check(c[0] > 60 && c[2] > 60 && c[1] < 0.35 * c[0]
            && std::fabs(c[0] - c[2]) < 0.15 * c[0],
        "outOffset adds blue on top of the mixed red");
  }
  anariUnsetParameter(dev, rc.color, "outTransform");
  anariUnsetParameter(dev, rc.color, "outOffset");
  anariCommitParameters(dev, rc.color);
  expectQuadrants(rc, "RGBW", "unsetting output transforms restores texels");

  // --- Scalar slot swizzle / mixing on the opacity sampler.
  Mat4 useG;
  useG.set(0, 0, 0).set(1, 0, 1);
  setMat4(dev, rc.opacity, "outTransform", useG.m);
  anariCommitParameters(dev, rc.opacity);
  expectAlpha(rc, 255, 0, "opacity swizzled to the green channel");
  Mat4 halfRG;
  halfRG.set(0, 0, 0.5f).set(1, 0, 0.5f);
  setMat4(dev, rc.opacity, "outTransform", halfRG.m);
  anariCommitParameters(dev, rc.opacity);
  expectAlpha(rc, 255, 64, "opacity mixes 0.5 r + 0.5 g");
  setVec4(dev, rc.opacity, "outOffset", 0.25f, 0, 0, 0);
  anariCommitParameters(dev, rc.opacity);
  expectAlpha(rc, 255, 128, "opacity mixing plus outOffset");
  anariUnsetParameter(dev, rc.opacity, "outTransform");
  anariUnsetParameter(dev, rc.opacity, "outOffset");
  anariCommitParameters(dev, rc.opacity);
  expectAlpha(rc, 255, 128, "opacity restored to red");

  // --- image1D on a non-default attribute: 2 texels R, G along attribute1.x.
  {
    static const uint8_t ramp[2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255};
    ANARIArray1D img =
        anariNewArray1D(dev, ramp, nullptr, nullptr, ANARI_UFIXED8_VEC4, 2);
    ANARISampler s1 = anariNewSampler(dev, "image1D");
    anariSetParameter(dev, s1, "image", ANARI_ARRAY1D, &img);
    setStr(dev, s1, "filter", "nearest");
    setStr(dev, s1, "inAttribute", "attribute1");
    anariCommitParameters(dev, s1);
    anariSetParameter(dev, rc.mat, "baseColor", ANARI_SAMPLER, &s1);
    anariCommitParameters(dev, rc.mat);
    expectQuadrants(rc, "GRGR", "image1D reads attribute1.x");
    anariSetParameter(dev, rc.mat, "baseColor", ANARI_SAMPLER, &rc.color);
    anariCommitParameters(dev, rc.mat);
    anariRelease(dev, s1);
    anariRelease(dev, img);
  }
  expectQuadrants(rc, "RGBW", "rebinding the 2D sampler restores the checker");

  releaseRig(rc);
  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
      g_errors);
  return g_errors == 0 ? 0 : 1;
}
