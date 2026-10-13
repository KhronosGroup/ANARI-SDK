// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises KHR_SAMPLER_IMAGExD_CLAMP_TO_BORDER (SPEC §6.4). A quad carries st
// over [-1, 2]² — a full texture inside, a full texture's width outside on
// every side — so every edge and corner of the image is sampled from inside,
// on, and outside. The sampler feeds a physicallyBased `emissive` on an unlit
// black base rendered into a FLOAT32 frame (the numeric readback of
// render_image_precision), and each pixel is compared with a CPU reference of
// the ANARI contract: per-axis wrap (clampToEdge / repeat / mirrorRepeat /
// clampToBorder), the RGBA `borderColor`, nearest and bilinear filters, then
// `outTransform · value + outOffset`. Covered: zero and nonzero RGBA borders
// (the border's alpha is read through a swizzling outTransform), mixed per-axis
// wrap modes, non-square images, rotated inTransforms on the UsdTransform2d
// and baked coordinate paths, image1D, and border edits on a live sampler.

#include <anari/anari.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_errors = 0;

static void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_WARNING)
    std::printf("[%s] %s\n",
        severity <= ANARI_SEVERITY_ERROR ? "ERR" : "WARN",
        message);
  if (severity <= ANARI_SEVERITY_ERROR)
    ++g_errors;
}

static void check(bool ok, const char *what)
{
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok)
    ++g_errors;
}

// --- CPU reference of the ANARI image sampler ------------------------------

struct Vec4
{
  float v[4] = {0, 0, 0, 0};
  float &operator[](int i)
  {
    return v[i];
  }
  float operator[](int i) const
  {
    return v[i];
  }
};

static Vec4 mix(const Vec4 &a, const Vec4 &b, float t)
{
  Vec4 r;
  for (int k = 0; k < 4; ++k)
    r[k] = a[k] * (1.f - t) + b[k] * t;
  return r;
}

enum class Wrap
{
  ClampToEdge,
  Repeat,
  MirrorRepeat,
  ClampToBorder
};

static const char *wrapName(Wrap w)
{
  switch (w) {
  case Wrap::ClampToEdge:
    return "clampToEdge";
  case Wrap::Repeat:
    return "repeat";
  case Wrap::MirrorRepeat:
    return "mirrorRepeat";
  case Wrap::ClampToBorder:
    return "clampToBorder";
  }
  return "";
}

// Wrap texel index i of an axis of n texels; false when it lands in the border.
static bool wrapIndex(int i, int n, Wrap w, int &out)
{
  switch (w) {
  case Wrap::ClampToEdge:
    out = i < 0 ? 0 : (i >= n ? n - 1 : i);
    return true;
  case Wrap::Repeat:
    out = ((i % n) + n) % n;
    return true;
  case Wrap::MirrorRepeat: {
    const int period = 2 * n;
    int m = ((i % period) + period) % period;
    out = m < n ? m : period - 1 - m;
    return true;
  }
  case Wrap::ClampToBorder:
    out = i;
    return i >= 0 && i < n;
  }
  return false;
}

struct RefSampler
{
  int W = 1, H = 1;
  std::vector<Vec4> texels; // row-major, row 0 = ANARI v=0
  Wrap wrapS = Wrap::ClampToEdge, wrapT = Wrap::ClampToEdge;
  bool nearest = true;
  Vec4 border;
  // inTransform (column-major mat4) and inOffset over (u, v, 0, 1).
  float inTransform[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  float inOffset[4] = {0, 0, 0, 0};
  float outTransform[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  float outOffset[4] = {0, 0, 0, 0};

  Vec4 fetch(int i, int j) const
  {
    int wi = 0, wj = 0;
    if (!wrapIndex(i, W, wrapS, wi) || !wrapIndex(j, H, wrapT, wj))
      return border;
    return texels[size_t(wj) * W + wi];
  }

  Vec4 sampleTexture(float u, float v) const
  {
    if (nearest)
      return fetch(int(std::floor(u * W)), int(std::floor(v * H)));
    const float x = u * W - 0.5f, y = v * H - 0.5f;
    const int i0 = int(std::floor(x)), j0 = int(std::floor(y));
    const float fx = x - i0, fy = y - j0;
    const Vec4 bottom = mix(fetch(i0, j0), fetch(i0 + 1, j0), fx);
    const Vec4 top = mix(fetch(i0, j0 + 1), fetch(i0 + 1, j0 + 1), fx);
    return mix(bottom, top, fy);
  }

  static Vec4 affine(const float m[16], const float o[4], const Vec4 &p)
  {
    Vec4 r;
    for (int row = 0; row < 4; ++row) {
      r[row] = o[row];
      for (int col = 0; col < 4; ++col)
        r[row] += m[4 * col + row] * p[col];
    }
    return r;
  }

  // The texture coordinate the input map produces for attribute (u, v).
  Vec4 texCoord(float u, float v) const
  {
    Vec4 a;
    a[0] = u;
    a[1] = v;
    a[2] = 0.f;
    a[3] = 1.f;
    return affine(inTransform, inOffset, a);
  }

  // Whether nearest sampling at (u, v) sits within `guard` texels of a texel
  // boundary on either axis (an ambiguous probe).
  bool nearBoundary(float u, float v, float guard) const
  {
    const Vec4 t = texCoord(u, v);
    const float x = t[0] * W, y = t[1] * H;
    return std::fabs(x - std::round(x)) < guard
        || std::fabs(y - std::round(y)) < guard;
  }

  // The sampler's value for attribute (u, v): input map, texture, output map.
  Vec4 value(float u, float v) const
  {
    const Vec4 t = texCoord(u, v);
    return affine(outTransform, outOffset, sampleTexture(t[0], t[1]));
  }
};

// --- Scene -----------------------------------------------------------------

struct Img
{
  uint32_t w = 0, h = 0;
  std::vector<float> rgba;
  const float *at(int x, int y) const
  {
    return &rgba[(size_t(y) * w + x) * 4];
  }
};

static constexpr int kSize = 256;
// st range over the quad: one texture inside, one texture width outside on
// each side.
static constexpr float kStMin = -1.f, kStMax = 2.f;

// The quad spans [-0.9, 0.9] viewed head-on from z=3 with fovy 0.6, so the
// image-plane mapping is affine. Pixel column holding attribute u, and the u
// its centre actually samples (the reference is evaluated there).
static int pixelFor(float st)
{
  const float halfExtent = 3.f * std::tan(0.3f);
  const float q = -0.9f + 1.8f * (st - kStMin) / (kStMax - kStMin);
  return int(std::floor((q / halfExtent + 1.f) * 0.5f * kSize));
}
static float stAtPixel(int p)
{
  const float halfExtent = 3.f * std::tan(0.3f);
  const float q = ((p + 0.5f) / kSize * 2.f - 1.f) * halfExtent;
  return kStMin + (q + 0.9f) / 1.8f * (kStMax - kStMin);
}

struct Scene
{
  ANARIDevice dev{};
  ANARIMaterial mat{};
  ANARIArray1D pos{}, st{};
  ANARIGeometry g{};
  ANARISurface surf{};
  ANARIArray1D surfArray{};
  ANARIWorld world{};
  ANARICamera cam{};
  ANARIRenderer ren{};
  ANARIFrame frame{};

  void build(ANARIDevice d, ANARISampler sampler)
  {
    dev = d;
    mat = anariNewMaterial(dev, "physicallyBased");
    const float black[3] = {0.f, 0.f, 0.f};
    const float one = 1.f, zero = 0.f;
    anariSetParameter(dev, mat, "baseColor", ANARI_FLOAT32_VEC3, black);
    anariSetParameter(dev, mat, "emissive", ANARI_SAMPLER, &sampler);
    anariSetParameter(dev, mat, "roughness", ANARI_FLOAT32, &one);
    anariSetParameter(dev, mat, "metallic", ANARI_FLOAT32, &zero);
    anariCommitParameters(dev, mat);

    static const float verts[12] = {
        -0.9f, -0.9f, 0.f, 0.9f, -0.9f, 0.f, 0.9f, 0.9f, 0.f, -0.9f, 0.9f, 0.f};
    static const float uv[8] = {
        kStMin, kStMin, kStMax, kStMin, kStMax, kStMax, kStMin, kStMax};
    pos = anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
    st = anariNewArray1D(dev, uv, nullptr, nullptr, ANARI_FLOAT32_VEC2, 4);
    g = anariNewGeometry(dev, "quad");
    anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
    anariSetParameter(dev, g, "vertex.attribute0", ANARI_ARRAY1D, &st);
    anariCommitParameters(dev, g);

    surf = anariNewSurface(dev);
    anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &g);
    anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
    anariCommitParameters(dev, surf);

    world = anariNewWorld(dev);
    surfArray = anariNewArray1D(dev, &surf, nullptr, nullptr, ANARI_SURFACE, 1);
    anariSetParameter(dev, world, "surface", ANARI_ARRAY1D, &surfArray);
    anariCommitParameters(dev, world);

    cam = anariNewCamera(dev, "perspective");
    const float eye[3] = {0.f, 0.f, 3.f};
    const float dir[3] = {0.f, 0.f, -1.f};
    const float up[3] = {0.f, 1.f, 0.f};
    const float fovy = 0.6f;
    anariSetParameter(dev, cam, "position", ANARI_FLOAT32_VEC3, eye);
    anariSetParameter(dev, cam, "direction", ANARI_FLOAT32_VEC3, dir);
    anariSetParameter(dev, cam, "up", ANARI_FLOAT32_VEC3, up);
    anariSetParameter(dev, cam, "fovy", ANARI_FLOAT32, &fovy);
    anariCommitParameters(dev, cam);

    ren = anariNewRenderer(dev, "default");
    const float background[4] = {0.f, 0.f, 0.f, 0.f};
    anariSetParameter(dev, ren, "background", ANARI_FLOAT32_VEC4, background);
    anariCommitParameters(dev, ren);

    frame = anariNewFrame(dev);
    const uint32_t size[2] = {kSize, kSize};
    const ANARIDataType colorFmt = ANARI_FLOAT32_VEC4;
    anariSetParameter(dev, frame, "size", ANARI_UINT32_VEC2, size);
    anariSetParameter(dev, frame, "channel.color", ANARI_DATA_TYPE, &colorFmt);
    anariSetParameter(dev, frame, "world", ANARI_WORLD, &world);
    anariSetParameter(dev, frame, "camera", ANARI_CAMERA, &cam);
    anariSetParameter(dev, frame, "renderer", ANARI_RENDERER, &ren);
    anariCommitParameters(dev, frame);
  }

  void render(Img &im)
  {
    anariRenderFrame(dev, frame);
    anariFrameReady(dev, frame, ANARI_WAIT);
    ANARIDataType pt = ANARI_UNKNOWN;
    const float *pixels = (const float *)anariMapFrame(
        dev, frame, "channel.color", &im.w, &im.h, &pt);
    im.rgba.clear();
    if (pixels && pt == ANARI_FLOAT32_VEC4)
      im.rgba.assign(pixels, pixels + size_t(im.w) * im.h * 4);
    anariUnmapFrame(dev, frame, "channel.color");
    if (im.rgba.empty()) {
      std::printf("  [FAIL] frame did not map as FLOAT32_VEC4\n");
      ++g_errors;
      im.rgba.assign(size_t(kSize) * kSize * 4, 0.f);
      im.w = im.h = kSize;
    }
  }

  void release()
  {
    for (ANARIObject o : {(ANARIObject)frame,
             (ANARIObject)ren,
             (ANARIObject)cam,
             (ANARIObject)surfArray,
             (ANARIObject)world,
             (ANARIObject)surf,
             (ANARIObject)g,
             (ANARIObject)st,
             (ANARIObject)pos,
             (ANARIObject)mat})
      anariRelease(dev, o);
  }
};

// Storm adds a small view-dependent shading term to an unlit black-base
// surface (~0.01, independent of the texture). Measured once from an all-black
// texture and subtracted per pixel.
static Img g_baseline;

// --- Cases -----------------------------------------------------------------

// A 4×2 image (non-square, so the two axes pad by different factors): distinct
// RGB per texel and distinct alpha, all off the border colours used below.
static const int kW = 4, kH = 2;
static const float kImage[kW * kH * 4] = {1.0f,
    0.0f,
    0.0f,
    1.00f,
    /**/ 0.0f,
    1.0f,
    0.0f,
    0.90f, /**/
    0.0f,
    0.0f,
    1.0f,
    0.80f,
    /**/ 1.0f,
    1.0f,
    0.0f,
    0.70f, /**/
    0.0f,
    1.0f,
    1.0f,
    0.60f,
    /**/ 1.0f,
    0.0f,
    1.0f,
    0.50f, /**/
    0.5f,
    0.5f,
    0.5f,
    0.40f,
    /**/ 1.0f,
    1.0f,
    1.0f,
    0.30f};

// Sample positions in st: far outside, just outside, on the edge, just inside,
// texel centres. "Just" is 0.05 st: 0.2 texel of the 4-wide axis and 0.1 texel
// of the 2-tall axis, several pixels from the discontinuities of nearest
// sampling. The on-edge probes are where linear filtering blends the border
// and edge texel 50/50; under nearest filtering they sit on a discontinuity
// (which side a pixel centre lands on is the rasteriser's sub-pixel call), so
// compare() skips a nearest probe within kNearestGuard texels of a boundary.
static const float kProbes[] = {-0.6f,
    -0.05f,
    0.f,
    0.05f,
    0.125f,
    0.375f,
    0.625f,
    0.875f,
    0.95f,
    1.f,
    1.05f,
    1.6f};
static constexpr float kNearestGuard = 0.03f;

struct Case
{
  std::string name;
  const char *subtype = "image2D";
  Wrap wrapS = Wrap::ClampToEdge, wrapT = Wrap::ClampToEdge;
  bool nearest = true;
  bool setBorder = false;
  Vec4 border;
  bool setInTransform = false;
  float inTransform[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  float inOffset[4] = {0, 0, 0, 0};
  bool setOutTransform = false;
  float outTransform[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  float outOffset[4] = {0, 0, 0, 0};
};

static void applyCase(ANARIDevice dev, ANARISampler s, const Case &c)
{
  const bool oneD = std::strcmp(c.subtype, "image1D") == 0;
  anariSetParameter(
      dev, s, oneD ? "wrapMode" : "wrapMode1", ANARI_STRING, wrapName(c.wrapS));
  if (!oneD)
    anariSetParameter(dev, s, "wrapMode2", ANARI_STRING, wrapName(c.wrapT));
  anariSetParameter(
      dev, s, "filter", ANARI_STRING, c.nearest ? "nearest" : "linear");
  if (c.setBorder)
    anariSetParameter(dev, s, "borderColor", ANARI_FLOAT32_VEC4, c.border.v);
  else
    anariUnsetParameter(dev, s, "borderColor");
  if (c.setInTransform) {
    anariSetParameter(dev, s, "inTransform", ANARI_FLOAT32_MAT4, c.inTransform);
    anariSetParameter(dev, s, "inOffset", ANARI_FLOAT32_VEC4, c.inOffset);
  } else {
    anariUnsetParameter(dev, s, "inTransform");
    anariUnsetParameter(dev, s, "inOffset");
  }
  if (c.setOutTransform) {
    anariSetParameter(
        dev, s, "outTransform", ANARI_FLOAT32_MAT4, c.outTransform);
    anariSetParameter(dev, s, "outOffset", ANARI_FLOAT32_VEC4, c.outOffset);
  } else {
    anariUnsetParameter(dev, s, "outTransform");
    anariUnsetParameter(dev, s, "outOffset");
  }
  anariCommitParameters(dev, s);
}

static RefSampler referenceFor(const Case &c)
{
  RefSampler r;
  const bool oneD = std::strcmp(c.subtype, "image1D") == 0;
  r.W = kW;
  r.H = oneD ? 1 : kH;
  r.texels.resize(size_t(r.W) * r.H);
  for (size_t i = 0; i < r.texels.size(); ++i)
    for (int k = 0; k < 4; ++k)
      r.texels[i][k] = kImage[4 * i + k];
  r.wrapS = c.wrapS;
  // A 1D texture ignores the second coordinate.
  r.wrapT = oneD ? Wrap::ClampToEdge : c.wrapT;
  r.nearest = c.nearest;
  if (c.setBorder)
    r.border = c.border;
  if (c.setInTransform) {
    std::memcpy(r.inTransform, c.inTransform, sizeof(r.inTransform));
    std::memcpy(r.inOffset, c.inOffset, sizeof(r.inOffset));
  }
  if (c.setOutTransform) {
    std::memcpy(r.outTransform, c.outTransform, sizeof(r.outTransform));
    std::memcpy(r.outOffset, c.outOffset, sizeof(r.outOffset));
  }
  return r;
}

// Compare the rendered frame against the reference over the probe grid.
// Tolerance: the half-float colour buffer (2^-11 relative), the baseline
// subtraction, and, under linear filtering, the sub-pixel offset between the
// probe and the pixel centre (the reference is evaluated at the pixel centre,
// so this is only the rasteriser's own half-pixel uncertainty over a gradient
// of at most one colour unit per texel).
static void compare(const Img &im, const RefSampler &ref, const char *what)
{
  int bad = 0, total = 0, skipped = 0;
  float worst = 0.f;
  std::string worstWhere;
  const float tol = ref.nearest ? 0.02f : 0.03f;
  for (float pu : kProbes) {
    for (float pv : kProbes) {
      const int px = pixelFor(pu), py = pixelFor(pv);
      const float u = stAtPixel(px), v = stAtPixel(py);
      if (ref.nearest && ref.nearBoundary(u, v, kNearestGuard)) {
        ++skipped;
        continue;
      }
      const Vec4 want = ref.value(u, v);
      const float *p = im.at(px, py);
      const float *b = g_baseline.at(px, py);
      float err = 0.f;
      for (int k = 0; k < 3; ++k) {
        const float got = p[k] - b[k];
        err = std::max(
            err, std::fabs(got - want[k]) - 1e-3f * std::fabs(want[k]));
      }
      ++total;
      if (err > tol) {
        ++bad;
        if (bad <= 3)
          std::printf(
              "    mismatch at st=(%.3f,%.3f) px=(%d,%d): got %.4f "
              "%.4f %.4f want %.4f %.4f %.4f\n",
              u,
              v,
              px,
              py,
              p[0] - b[0],
              p[1] - b[1],
              p[2] - b[2],
              want[0],
              want[1],
              want[2]);
      }
      if (err > worst) {
        worst = err;
        worstWhere = "st=(" + std::to_string(u) + "," + std::to_string(v) + ")";
      }
    }
  }
  std::printf(
      "    %d/%d probes within %.3f (worst %.4f at %s), %d ambiguous "
      "nearest probes skipped\n",
      total - bad,
      total,
      tol,
      worst,
      worstWhere.c_str(),
      skipped);
  check(bad == 0, what);
}

static void runCase(ANARIDevice dev, ANARISampler s, const Case &c)
{
  std::printf("  case: %s\n", c.name.c_str());
  applyCase(dev, s, c);
  Scene sc;
  sc.build(dev, s);
  Img im;
  sc.render(im);
  sc.release();
  compare(im, referenceFor(c), c.name.c_str());
}

int main(int argc, char **argv)
{
  const char *subtype = argc > 1 ? argv[1] : "storm";
  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  if (!lib)
    return 2;
  ANARIDevice dev = anariNewDevice(lib, subtype);
  if (!dev)
    return 2;

  ANARIArray2D image2D = anariNewArray2D(
      dev, kImage, nullptr, nullptr, ANARI_FLOAT32_VEC4, kW, kH);
  ANARIArray1D image1D =
      anariNewArray1D(dev, kImage, nullptr, nullptr, ANARI_FLOAT32_VEC4, kW);

  // --- Baseline: an all-black texture measures the unlit shading term -------
  {
    std::vector<float> black(size_t(kW) * kH * 4, 0.f);
    for (size_t i = 0; i < size_t(kW) * kH; ++i)
      black[4 * i + 3] = 1.f;
    ANARIArray2D img = anariNewArray2D(
        dev, black.data(), nullptr, nullptr, ANARI_FLOAT32_VEC4, kW, kH);
    ANARISampler s = anariNewSampler(dev, "image2D");
    anariSetParameter(dev, s, "image", ANARI_ARRAY2D, &img);
    anariCommitParameters(dev, s);
    Scene sc;
    sc.build(dev, s);
    sc.render(g_baseline);
    sc.release();
    const float *p = g_baseline.at(kSize / 2, kSize / 2);
    std::printf("black baseline = %.5f %.5f %.5f\n", p[0], p[1], p[2]);
    check(p[0] < 0.05f && p[1] < 0.05f && p[2] < 0.05f,
        "black emissive texture renders near black (baseline)");
    anariRelease(dev, s);
    anariRelease(dev, img);
  }

  ANARISampler s2 = anariNewSampler(dev, "image2D");
  anariSetParameter(dev, s2, "image", ANARI_ARRAY2D, &image2D);
  ANARISampler s1 = anariNewSampler(dev, "image1D");
  anariSetParameter(dev, s1, "image", ANARI_ARRAY1D, &image1D);

  const Vec4 zero;
  Vec4 tint;
  tint[0] = 0.2f;
  tint[1] = 0.4f;
  tint[2] = 0.6f;
  tint[3] = 0.8f;
  Vec4 alphaOnly;
  alphaOnly[3] = 0.6f;

  std::vector<Case> cases;
  for (bool nearest : {true, false}) {
    const std::string f = nearest ? " / nearest" : " / linear";
    // Reference: the existing wrap modes still match the CPU contract.
    {
      Case c;
      c.name = "clampToEdge both axes" + f;
      c.nearest = nearest;
      cases.push_back(c);
    }
    // Default (zero) border on both axes: Storm's black wrap.
    {
      Case c;
      c.name = "clampToBorder both axes, default zero border" + f;
      c.wrapS = c.wrapT = Wrap::ClampToBorder;
      c.nearest = nearest;
      cases.push_back(c);
    }
    // Explicit zero border.
    {
      Case c;
      c.name = "clampToBorder both axes, explicit zero border" + f;
      c.wrapS = c.wrapT = Wrap::ClampToBorder;
      c.nearest = nearest;
      c.setBorder = true;
      c.border = zero;
      cases.push_back(c);
    }
    // Nonzero RGBA border: the padded-image path.
    {
      Case c;
      c.name = "clampToBorder both axes, RGBA border" + f;
      c.wrapS = c.wrapT = Wrap::ClampToBorder;
      c.nearest = nearest;
      c.setBorder = true;
      c.border = tint;
      cases.push_back(c);
    }
    // Mixed per-axis modes, nonzero border: only one axis pads.
    {
      Case c;
      c.name = "clampToBorder S + repeat T, RGBA border" + f;
      c.wrapS = Wrap::ClampToBorder;
      c.wrapT = Wrap::Repeat;
      c.nearest = nearest;
      c.setBorder = true;
      c.border = tint;
      cases.push_back(c);
    }
    {
      Case c;
      c.name = "mirrorRepeat S + clampToBorder T, RGBA border" + f;
      c.wrapS = Wrap::MirrorRepeat;
      c.wrapT = Wrap::ClampToBorder;
      c.nearest = nearest;
      c.setBorder = true;
      c.border = tint;
      cases.push_back(c);
    }
    {
      Case c;
      c.name = "clampToEdge S + clampToBorder T, zero border" + f;
      c.wrapS = Wrap::ClampToEdge;
      c.wrapT = Wrap::ClampToBorder;
      c.nearest = nearest;
      cases.push_back(c);
    }
    // The border's alpha, read through a swizzling outTransform (alpha -> red,
    // green -> green, blue -> blue): a zero-RGB border with nonzero alpha must
    // not be mistaken for Storm's transparent-black border.
    {
      Case c;
      c.name = "clampToBorder, alpha-only border read via swizzle" + f;
      c.wrapS = c.wrapT = Wrap::ClampToBorder;
      c.nearest = nearest;
      c.setBorder = true;
      c.border = alphaOnly;
      c.setOutTransform = true;
      std::memset(c.outTransform, 0, sizeof(c.outTransform));
      c.outTransform[12] = 1.f; // red   <- alpha
      c.outTransform[5] = 1.f; // green <- green
      c.outTransform[10] = 1.f; // blue  <- blue
      c.outTransform[15] = 1.f;
      cases.push_back(c);
    }
    // Zero border with an outOffset: the border reads as the offset.
    {
      Case c;
      c.name = "clampToBorder, zero border + outOffset" + f;
      c.wrapS = c.wrapT = Wrap::ClampToBorder;
      c.nearest = nearest;
      c.setOutTransform = true;
      c.outOffset[0] = 0.1f;
      c.outOffset[1] = 0.2f;
      c.outOffset[2] = 0.3f;
      cases.push_back(c);
    }
    // Zero border with a channel-mixing outTransform and an outOffset: the
    // mixed border is the offset, which is not transparent black.
    {
      Case c;
      c.name = "clampToBorder, zero border + swizzle + outOffset" + f;
      c.wrapS = c.wrapT = Wrap::ClampToBorder;
      c.nearest = nearest;
      c.setOutTransform = true;
      std::memset(c.outTransform, 0, sizeof(c.outTransform));
      c.outTransform[4] = 1.f; // red   <- green
      c.outTransform[8] = 1.f; // green <- blue
      c.outTransform[2] = 1.f; // blue  <- red
      c.outTransform[15] = 1.f;
      c.outOffset[0] = 0.25f;
      cases.push_back(c);
    }
    // Rotated inTransform by 90° with a nonzero border on a non-square image:
    // the padding remap composes with the rotation on the UsdTransform2d path.
    {
      Case c;
      c.name = "clampToBorder, RGBA border, 90-degree inTransform" + f;
      c.wrapS = c.wrapT = Wrap::ClampToBorder;
      c.nearest = nearest;
      c.setBorder = true;
      c.border = tint;
      c.setInTransform = true;
      std::memset(c.inTransform, 0, sizeof(c.inTransform));
      c.inTransform[1] = 1.f; // u' = -v + 1
      c.inTransform[4] = -1.f; // v' = u
      c.inTransform[10] = 1.f;
      c.inTransform[15] = 1.f;
      c.inOffset[0] = 1.f;
      cases.push_back(c);
    }
    // Rotated by 30°: not decomposable once composed with per-axis padding, so
    // the coordinates bake into a derived primvar.
    {
      Case c;
      c.name = "clampToBorder, RGBA border, 30-degree inTransform (baked)" + f;
      c.wrapS = c.wrapT = Wrap::ClampToBorder;
      c.nearest = nearest;
      c.setBorder = true;
      c.border = tint;
      c.setInTransform = true;
      const float th = 30.f * float(M_PI) / 180.f;
      std::memset(c.inTransform, 0, sizeof(c.inTransform));
      c.inTransform[0] = std::cos(th);
      c.inTransform[1] = std::sin(th);
      c.inTransform[4] = -std::sin(th);
      c.inTransform[5] = std::cos(th);
      c.inTransform[10] = 1.f;
      c.inTransform[15] = 1.f;
      c.inOffset[0] = 0.3f;
      c.inOffset[1] = -0.1f;
      cases.push_back(c);
    }
    // image1D: `wrapMode` on the single axis, nonzero border.
    {
      Case c;
      c.name = "image1D clampToBorder, RGBA border" + f;
      c.subtype = "image1D";
      c.wrapS = Wrap::ClampToBorder;
      c.nearest = nearest;
      c.setBorder = true;
      c.border = tint;
      cases.push_back(c);
    }
    {
      Case c;
      c.name = "image1D clampToBorder, zero border" + f;
      c.subtype = "image1D";
      c.wrapS = Wrap::ClampToBorder;
      c.nearest = nearest;
      cases.push_back(c);
    }
  }

  // Every case runs as an edit of the same live sampler objects (the previous
  // case's parameters are overwritten or unset), so border behaviour after
  // sampler edits — including switching between the black-wrap and padded
  // paths and back to clampToEdge — is what is verified throughout.
  for (const Case &c : cases)
    runCase(dev, std::strcmp(c.subtype, "image1D") == 0 ? s1 : s2, c);

  // And an explicit edit sequence on one sampler with no scene rebuild.
  {
    std::printf("  live edit sequence on one sampler\n");
    Case c;
    c.wrapS = c.wrapT = Wrap::ClampToEdge;
    c.nearest = false;
    applyCase(dev, s2, c);
    Scene sc;
    sc.build(dev, s2);
    Img im;
    sc.render(im);
    compare(im, referenceFor(c), "live: clampToEdge");

    c.wrapS = c.wrapT = Wrap::ClampToBorder;
    c.setBorder = true;
    c.border = tint;
    applyCase(dev, s2, c);
    sc.render(im);
    compare(im, referenceFor(c), "live: edit to clampToBorder + RGBA border");

    c.border = zero;
    applyCase(dev, s2, c);
    sc.render(im);
    compare(im, referenceFor(c), "live: edit border to zero");

    c.border = tint;
    c.border[1] = 0.9f;
    applyCase(dev, s2, c);
    sc.render(im);
    compare(im, referenceFor(c), "live: edit border back to nonzero");

    c.wrapT = Wrap::Repeat;
    applyCase(dev, s2, c);
    sc.render(im);
    compare(im, referenceFor(c), "live: edit one axis to repeat");

    c.wrapS = c.wrapT = Wrap::ClampToEdge;
    c.setBorder = false;
    applyCase(dev, s2, c);
    sc.render(im);
    compare(im, referenceFor(c), "live: unset back to clampToEdge");
    sc.release();
  }

  anariRelease(dev, s1);
  anariRelease(dev, s2);
  anariRelease(dev, image1D);
  anariRelease(dev, image2D);
  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
      g_errors);
  return g_errors == 0 ? 0 : 1;
}
