// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises the primitive and transform samplers and the geometry-derived
// inAttribute sources (SPEC §6.4): primitive lookups by ANARI primitive index
// with inOffset and out-of-range reads, transform samplers over objectPosition /
// worldPosition / objectNormal / worldNormal / primitiveId and per-vertex
// attributes, image samplers on those same sources (the forced baked path),
// scalar slots, and live edits of all of it.
//
// Two surfaces share one material: a `quad` geometry with two quads in the
// lower half whose primitive.index lists the RIGHT quad first (so ANARI
// primitive 0 is the right quad although its vertices come last — the
// index / primitive-id distinction), and a `sphere` geometry with two spheres
// in the upper half (primitive 0 on the left). The sphere is generated
// geometry: every tessellated face must map back to its ANARI primitive, and
// its derived data must not be a copy of the quad rprim's. Four probes
// (left quad, right quad, left sphere, right sphere) classify each region's
// dominant color.

#include <anari/anari.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_errors = 0;

static void statusFunc(const void *, ANARIDevice, ANARIObject, ANARIDataType,
    ANARIStatusSeverity severity, ANARIStatusCode, const char *message)
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
  const uint8_t *at(int x, int y) const { return &rgba[(size_t(y) * w + x) * 4]; }
};

// Classify a color by channel ratios (robust to shading): R/G/B/W for the
// primaries, K for black, ? for a blend.
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

struct Rig
{
  ANARIDevice dev{};
  ANARIMaterial mat{};
  ANARIArray1D quadPos{}, quadIndex{}, quadNormals{}, spherePos{}, sphereRad{};
  ANARIGeometry quads{}, spheres{};
  ANARISurface quadSurf{}, sphereSurf{};
  ANARIArray1D surfArray{};
  ANARIWorld world{};
  ANARICamera cam{};
  ANARIRenderer ren{};
  ANARIFrame frame{};

  // Probe pixel centers: left quad, right quad, left sphere, right sphere.
  int px[4]{}, py[4]{};
  int r = 4;
};

static const float kFovy = 0.6f;
static const float kEyeZ = 3.f;
static const uint32_t kSize = 256;

static void setMat4(ANARIDevice dev, ANARIObject o, const char *name,
    const float m[16])
{
  anariSetParameter(dev, o, name, ANARI_FLOAT32_MAT4, m);
}

static void setVec4(ANARIDevice dev, ANARIObject o, const char *name,
    float x, float y, float z, float w)
{
  const float v[4] = {x, y, z, w};
  anariSetParameter(dev, o, name, ANARI_FLOAT32_VEC4, v);
}

static void setStr(ANARIDevice dev, ANARIObject o, const char *name,
    const char *v)
{
  anariSetParameter(dev, o, name, ANARI_STRING, v);
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
  // Zero the 3x3 color block so only the terms set afterwards contribute.
  Mat4 &clear3()
  {
    for (int c = 0; c < 3; ++c)
      for (int r = 0; r < 3; ++r)
        set(c, r, 0.f);
    return *this;
  }
};

static void toPixel(float x, float y, int &px, int &py)
{
  const float half = kEyeZ * std::tan(kFovy / 2.f);
  px = int((0.5f + x / (2.f * half)) * kSize);
  py = int((0.5f + y / (2.f * half)) * kSize);   // frame rows run bottom-up
}

static void buildRig(Rig &rc)
{
  ANARIDevice dev = rc.dev;

  rc.mat = anariNewMaterial(dev, "physicallyBased");
  setStr(dev, rc.mat, "alphaMode", "blend");
  anariCommitParameters(dev, rc.mat);

  // Quads: verts 0..3 = left quad, 4..7 = right quad; primitive.index lists
  // the right quad first, so ANARI primitive 0 is the RIGHT quad.
  static const float quadVerts[8 * 3] = {
      -0.9f, -0.9f, 0.f, -0.1f, -0.9f, 0.f, -0.1f, -0.1f, 0.f, -0.9f, -0.1f, 0.f,
      0.1f, -0.9f, 0.f, 0.9f, -0.9f, 0.f, 0.9f, -0.1f, 0.f, 0.1f, -0.1f, 0.f};
  static const uint32_t quadIdx[2 * 4] = {4, 5, 6, 7, 0, 1, 2, 3};
  rc.quadPos = anariNewArray1D(
      dev, quadVerts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 8);
  rc.quadIndex = anariNewArray1D(
      dev, quadIdx, nullptr, nullptr, ANARI_UINT32_VEC4, 2);
  rc.quads = anariNewGeometry(dev, "quad");
  anariSetParameter(dev, rc.quads, "vertex.position", ANARI_ARRAY1D, &rc.quadPos);
  anariSetParameter(dev, rc.quads, "primitive.index", ANARI_ARRAY1D, &rc.quadIndex);
  anariCommitParameters(dev, rc.quads);

  // App-provided normals (all +z), attached later in the normal tests.
  static const float quadNrm[8 * 3] = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1,
      0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
  rc.quadNormals = anariNewArray1D(
      dev, quadNrm, nullptr, nullptr, ANARI_FLOAT32_VEC3, 8);

  // Spheres: primitive 0 on the left, 1 on the right (no index).
  static const float sphereCenters[2 * 3] = {-0.5f, 0.5f, 0.f, 0.5f, 0.5f, 0.f};
  static const float sphereRadii[2] = {0.4f, 0.4f};
  rc.spherePos = anariNewArray1D(
      dev, sphereCenters, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
  rc.sphereRad = anariNewArray1D(
      dev, sphereRadii, nullptr, nullptr, ANARI_FLOAT32, 2);
  rc.spheres = anariNewGeometry(dev, "sphere");
  anariSetParameter(dev, rc.spheres, "vertex.position", ANARI_ARRAY1D, &rc.spherePos);
  anariSetParameter(dev, rc.spheres, "vertex.radius", ANARI_ARRAY1D, &rc.sphereRad);
  anariCommitParameters(dev, rc.spheres);

  rc.quadSurf = anariNewSurface(dev);
  anariSetParameter(dev, rc.quadSurf, "geometry", ANARI_GEOMETRY, &rc.quads);
  anariSetParameter(dev, rc.quadSurf, "material", ANARI_MATERIAL, &rc.mat);
  anariCommitParameters(dev, rc.quadSurf);
  rc.sphereSurf = anariNewSurface(dev);
  anariSetParameter(dev, rc.sphereSurf, "geometry", ANARI_GEOMETRY, &rc.spheres);
  anariSetParameter(dev, rc.sphereSurf, "material", ANARI_MATERIAL, &rc.mat);
  anariCommitParameters(dev, rc.sphereSurf);

  rc.world = anariNewWorld(dev);
  const ANARISurface surfs[2] = {rc.quadSurf, rc.sphereSurf};
  rc.surfArray = anariNewArray1D(dev, surfs, nullptr, nullptr, ANARI_SURFACE, 2);
  anariSetParameter(dev, rc.world, "surface", ANARI_ARRAY1D, &rc.surfArray);
  anariCommitParameters(dev, rc.world);

  rc.cam = anariNewCamera(dev, "perspective");
  const float eye[3] = {0.f, 0.f, kEyeZ};
  const float dir[3] = {0.f, 0.f, -1.f};
  const float up[3] = {0.f, 1.f, 0.f};
  anariSetParameter(dev, rc.cam, "position", ANARI_FLOAT32_VEC3, eye);
  anariSetParameter(dev, rc.cam, "direction", ANARI_FLOAT32_VEC3, dir);
  anariSetParameter(dev, rc.cam, "up", ANARI_FLOAT32_VEC3, up);
  anariSetParameter(dev, rc.cam, "fovy", ANARI_FLOAT32, &kFovy);
  anariCommitParameters(dev, rc.cam);

  rc.ren = anariNewRenderer(dev, "default");
  const float background[4] = {0, 0, 0, 0};
  anariSetParameter(dev, rc.ren, "background", ANARI_FLOAT32_VEC4, background);
  anariCommitParameters(dev, rc.ren);

  rc.frame = anariNewFrame(dev);
  const uint32_t size[2] = {kSize, kSize};
  const ANARIDataType colorFmt = ANARI_UFIXED8_RGBA_SRGB;
  anariSetParameter(dev, rc.frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(dev, rc.frame, "channel.color", ANARI_DATA_TYPE, &colorFmt);
  anariSetParameter(dev, rc.frame, "world", ANARI_WORLD, &rc.world);
  anariSetParameter(dev, rc.frame, "camera", ANARI_CAMERA, &rc.cam);
  anariSetParameter(dev, rc.frame, "renderer", ANARI_RENDERER, &rc.ren);
  anariCommitParameters(dev, rc.frame);

  toPixel(-0.5f, -0.5f, rc.px[0], rc.py[0]);
  toPixel(0.5f, -0.5f, rc.px[1], rc.py[1]);
  toPixel(-0.5f, 0.5f, rc.px[2], rc.py[2]);
  toPixel(0.5f, 0.5f, rc.px[3], rc.py[3]);
}

static void releaseRig(Rig &rc)
{
  ANARIDevice dev = rc.dev;
  for (ANARIObject o : {(ANARIObject)rc.frame, (ANARIObject)rc.ren,
           (ANARIObject)rc.cam, (ANARIObject)rc.surfArray,
           (ANARIObject)rc.world, (ANARIObject)rc.quadSurf,
           (ANARIObject)rc.sphereSurf, (ANARIObject)rc.quads,
           (ANARIObject)rc.spheres, (ANARIObject)rc.quadIndex,
           (ANARIObject)rc.quadPos, (ANARIObject)rc.quadNormals,
           (ANARIObject)rc.spherePos, (ANARIObject)rc.sphereRad,
           (ANARIObject)rc.mat})
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

// Classes at the four probes: "<left quad><right quad><left sphere><right sphere>".
static std::string probes(const Rig &rc, const Img &im)
{
  std::string out;
  for (int i = 0; i < 4; ++i) {
    double c[4];
    windowAvg(im, rc.px[i], rc.py[i], rc.r, c);
    out += classify(c[0], c[1], c[2]);
  }
  return out;
}

static void expectProbes(Rig &rc, const char *expected, const char *what)
{
  Img im;
  render(rc, im);
  const std::string got = probes(rc, im);
  check(got == expected,
      std::string(what) + " -> " + got + " (expected " + expected + ")");
}

static void expectAlpha(Rig &rc, const int expected[4], const char *what)
{
  Img im;
  render(rc, im);
  bool ok = true;
  char buf[200];
  int n = std::snprintf(buf, sizeof buf, "%s -> alpha", what);
  for (int i = 0; i < 4; ++i) {
    double c[4];
    windowAvg(im, rc.px[i], rc.py[i], rc.r, c);
    ok = ok && std::fabs(c[3] - expected[i]) <= 3;
    n += std::snprintf(buf + n, sizeof buf - n, " %.0f", c[3]);
  }
  std::snprintf(buf + n, sizeof buf - n, " (expected %d %d %d %d)",
      expected[0], expected[1], expected[2], expected[3]);
  check(ok, buf);
}

static void bindColor(Rig &rc, ANARISampler s)
{
  anariSetParameter(rc.dev, rc.mat, "baseColor", ANARI_SAMPLER, &s);
  anariCommitParameters(rc.dev, rc.mat);
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

  Rig rc;
  rc.dev = dev;
  buildRig(rc);

  // --- Baseline: the constant color covers all four probes.
  {
    const float white[3] = {1.f, 1.f, 1.f};
    anariSetParameter(dev, rc.mat, "baseColor", ANARI_FLOAT32_VEC3, white);
    anariCommitParameters(dev, rc.mat);
    expectProbes(rc, "WWWW", "baseline constant color covers every probe");
  }

  // === Primitive sampler ====================================================
  // array = [R, G, B, W]: ANARI primitive 0 (right quad / left sphere) reads
  // R, primitive 1 (left quad / right sphere) reads G.
  static uint8_t prims[4 * 4] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255,
      255, 255, 255, 255};
  ANARIArray1D primArray =
      anariNewArray1D(dev, prims, nullptr, nullptr, ANARI_UFIXED8_VEC4, 4);
  ANARISampler prim = anariNewSampler(dev, "primitive");
  anariSetParameter(dev, prim, "array", ANARI_ARRAY1D, &primArray);
  anariCommitParameters(dev, prim);
  bindColor(rc, prim);
  expectProbes(rc, "GRRG", "primitive sampler indexes by ANARI primitive, not vertex order");

  // inOffset shifts the lookup: primitive 0 -> B, 1 -> W.
  {
    const uint64_t offset = 2;
    anariSetParameter(dev, prim, "inOffset", ANARI_UINT64, &offset);
    anariCommitParameters(dev, prim);
    expectProbes(rc, "WBBW", "primitive sampler inOffset=2");
  }
  // Out of range past the end reads ANARI's missing value (0,0,0,1) -> black.
  {
    const uint64_t offset = 3;
    anariSetParameter(dev, prim, "inOffset", ANARI_UINT64, &offset);
    anariCommitParameters(dev, prim);
    expectProbes(rc, "KWWK", "primitive sampler out-of-range reads (0,0,0,1)");
    anariUnsetParameter(dev, prim, "inOffset");
    anariCommitParameters(dev, prim);
    expectProbes(rc, "GRRG", "unsetting inOffset restores the lookup");
  }
  // In-place array edit: [G, R, W, B].
  {
    uint8_t *p = (uint8_t *)anariMapArray(dev, primArray);
    static const uint8_t edited[4 * 4] = {0, 255, 0, 255, 255, 0, 0, 255,
        255, 255, 255, 255, 0, 0, 255, 255};
    std::memcpy(p, edited, sizeof edited);
    anariUnmapArray(dev, primArray);
    expectProbes(rc, "RGGR", "in-place edit of the primitive array");
  }
  // Replace the array with a FLOAT32_VEC3 of two elements [B, R].
  {
    static const float rgb[2 * 3] = {0, 0, 1, 1, 0, 0};
    ANARIArray1D replaced =
        anariNewArray1D(dev, rgb, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
    anariSetParameter(dev, prim, "array", ANARI_ARRAY1D, &replaced);
    anariCommitParameters(dev, prim);
    expectProbes(rc, "RBBR", "replacing the primitive array (float3)");
    anariRelease(dev, replaced);
  }
  // Scalar slot: a FLOAT32 array [1, 0.25] on opacity reads per primitive.
  {
    static const float alpha[2] = {1.f, 0.25f};
    ANARIArray1D alphaArray =
        anariNewArray1D(dev, alpha, nullptr, nullptr, ANARI_FLOAT32, 2);
    ANARISampler opacity = anariNewSampler(dev, "primitive");
    anariSetParameter(dev, opacity, "array", ANARI_ARRAY1D, &alphaArray);
    anariCommitParameters(dev, opacity);
    anariSetParameter(dev, rc.mat, "opacity", ANARI_SAMPLER, &opacity);
    anariCommitParameters(dev, rc.mat);
    // A closed sphere blends its back hemisphere behind the front, so the
    // 0.25 sphere reads 1 - 0.75^2 = 0.4375 (112); the single-layer quad 64.
    const int expected[4] = {64, 255, 255, 112};
    expectAlpha(rc, expected, "primitive sampler on the scalar opacity slot");
    anariUnsetParameter(dev, rc.mat, "opacity");
    anariCommitParameters(dev, rc.mat);
    anariRelease(dev, opacity);
    anariRelease(dev, alphaArray);
    const int opaque[4] = {255, 255, 255, 255};
    expectAlpha(rc, opaque, "unbinding the opacity sampler restores opaque");
  }

  // === Transform sampler ====================================================
  ANARISampler xf = anariNewSampler(dev, "transform");
  bindColor(rc, xf);

  // objectPosition: color = (0.5 + x, 0.5 - x, 0). x<0 -> G, x>0 -> R.
  {
    Mat4 m;
    m.clear3().set(0, 0, 1).set(0, 1, -1);
    setStr(dev, xf, "inAttribute", "objectPosition");
    setMat4(dev, xf, "outTransform", m.m);
    setVec4(dev, xf, "outOffset", 0.5f, 0.5f, 0.f, 0.f);
    anariCommitParameters(dev, xf);
    expectProbes(rc, "GRGR", "transform sampler on objectPosition.x");

    // color = (0.5 + y, 0, 0.5 - y): quads (y<0) -> B, spheres (y>0) -> R.
    Mat4 my;
    my.clear3().set(1, 0, 1).set(1, 2, -1);
    setMat4(dev, xf, "outTransform", my.m);
    setVec4(dev, xf, "outOffset", 0.5f, 0.f, 0.5f, 0.f);
    anariCommitParameters(dev, xf);
    expectProbes(rc, "BBRR", "transform sampler edit: objectPosition.y");

    // color = (2z, 0, 0): the flat quads are black; a sphere's front reads its
    // tessellated surface z (~radius), not its centre.
    Mat4 mz;
    mz.clear3().set(2, 0, 2);
    setMat4(dev, xf, "outTransform", mz.m);
    setVec4(dev, xf, "outOffset", 0.f, 0.f, 0.f, 0.f);
    anariCommitParameters(dev, xf);
    expectProbes(rc, "KKRR", "objectPosition.z reads the tessellated surface");

    // worldPosition equals objectPosition for world-direct surfaces.
    setStr(dev, xf, "inAttribute", "worldPosition");
    setMat4(dev, xf, "outTransform", m.m);
    setVec4(dev, xf, "outOffset", 0.5f, 0.5f, 0.f, 0.f);
    anariCommitParameters(dev, xf);
    expectProbes(rc, "GRGR", "transform sampler on worldPosition.x");
  }

  // objectNormal: color = (0.5 - 0.5 nz, 0, nz). The quads have no
  // app-provided normals (absent -> (0,0,0,1) -> R); the spheres' analytic
  // normal at the probed (oblique) hit points is near +z -> B. (Only nz is
  // mapped: the frame is sRGB-encoded, which would inflate a small nx.)
  {
    Mat4 m;
    m.clear3().set(2, 0, -0.5f).set(2, 2, 1.f);
    setStr(dev, xf, "inAttribute", "objectNormal");
    setMat4(dev, xf, "outTransform", m.m);
    setVec4(dev, xf, "outOffset", 0.5f, 0.f, 0.f, 0.f);
    anariCommitParameters(dev, xf);
    expectProbes(rc, "RRBB", "objectNormal: absent on the quads, +z on the spheres");

    anariSetParameter(dev, rc.quads, "vertex.normal", ANARI_ARRAY1D, &rc.quadNormals);
    anariCommitParameters(dev, rc.quads);
    expectProbes(rc, "BBBB", "objectNormal reads app-provided vertex.normal");

    setStr(dev, xf, "inAttribute", "worldNormal");
    anariCommitParameters(dev, xf);
    expectProbes(rc, "BBBB", "worldNormal equals objectNormal for world-direct surfaces");

    anariUnsetParameter(dev, rc.quads, "vertex.normal");
    anariCommitParameters(dev, rc.quads);
    expectProbes(rc, "RRBB", "removing vertex.normal re-derives the absent value");
  }

  // primitiveId: color = (1 - id, id, 0): primitive 0 -> R, 1 -> G.
  {
    Mat4 m;
    m.clear3().set(0, 0, -1).set(0, 1, 1);
    setStr(dev, xf, "inAttribute", "primitiveId");
    setMat4(dev, xf, "outTransform", m.m);
    setVec4(dev, xf, "outOffset", 1.f, 0.f, 0.f, 0.f);
    anariCommitParameters(dev, xf);
    expectProbes(rc, "GRRG", "transform sampler on primitiveId");
  }

  // A per-vertex attribute source with a swizzle: vertex.color on the quads
  // only, (1,0,0,1) left quad / (0,1,0,1) right quad; out swaps R and G. The
  // spheres have no color -> (0,0,0,1) swizzled -> black.
  {
    static const float colors[8 * 4] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1,
        1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1};
    ANARIArray1D colorArray =
        anariNewArray1D(dev, colors, nullptr, nullptr, ANARI_FLOAT32_VEC4, 8);
    anariSetParameter(dev, rc.quads, "vertex.color", ANARI_ARRAY1D, &colorArray);
    anariCommitParameters(dev, rc.quads);
    Mat4 swap;
    swap.clear3().set(0, 1, 1).set(1, 0, 1).set(2, 2, 1);
    setStr(dev, xf, "inAttribute", "color");
    setMat4(dev, xf, "outTransform", swap.m);
    anariUnsetParameter(dev, xf, "outOffset");
    anariCommitParameters(dev, xf);
    expectProbes(rc, "GRKK", "transform sampler swizzles vertex.color");
    anariUnsetParameter(dev, rc.quads, "vertex.color");
    anariCommitParameters(dev, rc.quads);
    anariRelease(dev, colorArray);
  }

  // Scalar slot: opacity = 1 - 0.75 * primitiveId.
  {
    ANARISampler op = anariNewSampler(dev, "transform");
    Mat4 m;
    m.clear3().set(0, 0, -0.75f);
    setStr(dev, op, "inAttribute", "primitiveId");
    setMat4(dev, op, "outTransform", m.m);
    setVec4(dev, op, "outOffset", 1.f, 0.f, 0.f, 0.f);
    anariCommitParameters(dev, op);
    anariSetParameter(dev, rc.mat, "opacity", ANARI_SAMPLER, &op);
    anariCommitParameters(dev, rc.mat);
    const int expected[4] = {64, 255, 255, 112};   // two layers on the sphere
    expectAlpha(rc, expected, "transform sampler on the scalar opacity slot");
    anariUnsetParameter(dev, rc.mat, "opacity");
    anariCommitParameters(dev, rc.mat);
    anariRelease(dev, op);
  }

  // === Image samplers on derived sources (forced baked path) ===============
  {
    // 2x1 image [R | G], nearest. primitiveId -> u = 0.5 id + 0.25.
    static const uint8_t ramp[2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255};
    ANARIArray2D img =
        anariNewArray2D(dev, ramp, nullptr, nullptr, ANARI_UFIXED8_VEC4, 2, 1);
    ANARISampler image = anariNewSampler(dev, "image2D");
    anariSetParameter(dev, image, "image", ANARI_ARRAY2D, &img);
    setStr(dev, image, "filter", "nearest");
    setStr(dev, image, "inAttribute", "primitiveId");
    Mat4 m;
    m.set(0, 0, 0.5f);
    setMat4(dev, image, "inTransform", m.m);
    setVec4(dev, image, "inOffset", 0.25f, 0.5f, 0.f, 0.f);
    anariCommitParameters(dev, image);
    bindColor(rc, image);
    expectProbes(rc, "GRRG", "image sampler on primitiveId");

    // objectPosition -> u = x + 0.5: left (x<0) -> R, right -> G.
    setStr(dev, image, "inAttribute", "objectPosition");
    anariUnsetParameter(dev, image, "inTransform");
    setVec4(dev, image, "inOffset", 0.5f, 0.5f, 0.f, 0.f);
    anariCommitParameters(dev, image);
    expectProbes(rc, "RGRG", "image sampler on objectPosition");

    anariRelease(dev, image);
    anariRelease(dev, img);
  }

  // Unbinding restores the constant.
  {
    const float white[3] = {1.f, 1.f, 1.f};
    anariSetParameter(dev, rc.mat, "baseColor", ANARI_FLOAT32_VEC3, white);
    anariCommitParameters(dev, rc.mat);
    expectProbes(rc, "WWWW", "unbinding the sampler restores the constant");
  }

  anariRelease(dev, xf);
  anariRelease(dev, prim);
  anariRelease(dev, primArray);
  releaseRig(rc);
  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", g_errors);
  return g_errors == 0 ? 0 : 1;
}
