// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises the normal-map material input (SPEC §6.1/§6.5 `normal`->`normal`).
// A physicallyBased material's `normal` is a sampler whose value IS the
// tangent-space normal: per the ANARI spec the app decodes [0,1] texels to
// [-1,1] through outTransform/outOffset (2 * texel - 1), and the device must
// not decode again. Storm builds the TBN frame from `st` (proven in
// prototypes/015-normal-map-tangents) and perturbs normals only from a texture,
// so transform/primitive samplers on `normal` go through a generated Storm
// surface shader that reads their baked primvar and builds the same st frame
// (SPEC §6.5).
//
// The quad is two quads side by side (primitive 0 left, primitive 1 right) with
// `st` spanning the unit square. A single directional light comes from the +X
// side, so a normal tilted toward +X renders brighter than a flat one and a
// normal tilted toward -X darker. Each variant rebinds `normal` and compares
// the two halves' luma with each other and with a flat (no normal map) render.
// Sharp and relative.

#include <anari/anari.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

static int g_errors = 0;
static int g_warnings = 0;

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
  if (severity == ANARI_SEVERITY_WARNING)
    ++g_warnings;
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

// Mean luma over columns [x0,x1) across all covered (non-background) rows.
static double halfLuma(const Img &im, int x0, int x1, long &covered)
{
  double s = 0;
  covered = 0;
  for (uint32_t y = 0; y < im.h; ++y)
    for (int x = x0; x < x1; ++x) {
      const uint8_t *p = im.at(x, y);
      if (p[0] > 20 || p[1] > 20 || p[2] > 20) {
        s += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
        ++covered;
      }
    }
  return covered ? s / covered : 0;
}

struct Halves
{
  double left{0}, right{0};
};

static Halves renderHalves(ANARIDevice dev, ANARIFrame frame, const char *what)
{
  anariRenderFrame(dev, frame);
  anariFrameReady(dev, frame, ANARI_WAIT);
  Img im;
  ANARIDataType pt = ANARI_UNKNOWN;
  const uint8_t *pixels = (const uint8_t *)anariMapFrame(
      dev, frame, "channel.color", &im.w, &im.h, &pt);
  if (pixels)
    im.rgba.assign(pixels, pixels + size_t(im.w) * im.h * 4);
  anariUnmapFrame(dev, frame, "channel.color");

  // Covered bounding box, then the two halves' mean luma.
  int x0 = im.w, x1 = 0;
  for (uint32_t y = 0; y < im.h; ++y)
    for (uint32_t x = 0; x < im.w; ++x) {
      const uint8_t *p = im.at(x, y);
      if (p[0] > 20 || p[1] > 20 || p[2] > 20) {
        x0 = std::min<int>(x0, x);
        x1 = std::max<int>(x1, x);
      }
    }
  const int mx = (x0 + x1) / 2;
  long lcov = 0, rcov = 0;
  Halves h;
  h.left = halfLuma(im, x0, mx, lcov);
  h.right = halfLuma(im, mx, x1 + 1, rcov);
  std::printf("  %-44s left=%.1f right=%.1f\n", what, h.left, h.right);
  check(x1 > x0 && lcov > 500 && rcov > 500,
      std::string(what) + ": both halves are covered");
  return h;
}

// ANARI mat4 is column-major.
static void setMat4(
    ANARIDevice d, ANARIObject o, const char *name, const float (&cols)[16])
{
  anariSetParameter(d, o, name, ANARI_FLOAT32_MAT4, cols);
}

static void setVec4(ANARIDevice d,
    ANARIObject o,
    const char *name,
    float x,
    float y,
    float z,
    float w)
{
  float v[4] = {x, y, z, w};
  anariSetParameter(d, o, name, ANARI_FLOAT32_VEC4, v);
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

  // Tangent-space normals, already in [-1,1].
  const float kTiltNeg[3] = {-0.6f, 0.f, 0.8f};
  const float kFlat[3] = {0.f, 0.f, 1.f};
  const float kTiltPos[3] = {0.6f, 0.f, 0.8f};

  // A 2x1 normal map: texel 0 (u<0.5) tilts -X, texel 1 (u>0.5) tilts +X.
  // UFIXED8_VEC4 packed n*0.5+0.5, so the app must decode it via outTransform.
  //   (-0.6,0,0.8) -> (51,127,229)   (0.6,0,0.8) -> (204,127,229)
  uint8_t nmap[2 * 1 * 4] = {51,
      127,
      229,
      255, /* u=0: tilt -X */
      204,
      127,
      229,
      255 /* u=1: tilt +X */};
  ANARIArray2D image =
      anariNewArray2D(dev, nmap, nullptr, nullptr, ANARI_UFIXED8_VEC4, 2, 1);
  // The same map as float texels already in [-1,1]: no decode needed.
  float fmap[2 * 1 * 3] = {kTiltNeg[0],
      kTiltNeg[1],
      kTiltNeg[2],
      kTiltPos[0],
      kTiltPos[1],
      kTiltPos[2]};
  ANARIArray2D fimage =
      anariNewArray2D(dev, fmap, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2, 1);
  // A flat packed map: every texel (0.5, 0.5, 1).
  uint8_t flatmap[2 * 1 * 4] = {128, 128, 255, 255, 128, 128, 255, 255};
  ANARIArray2D flatImage =
      anariNewArray2D(dev, flatmap, nullptr, nullptr, ANARI_UFIXED8_VEC4, 2, 1);

  // physicallyBased: white diffuse (metallic 0 / roughness 1 so a single
  // directional light shows Lambertian diffuse).
  ANARIMaterial mat = anariNewMaterial(dev, "physicallyBased");
  float white[3] = {1.f, 1.f, 1.f};
  float zero = 0.f, one = 1.f;
  anariSetParameter(dev, mat, "baseColor", ANARI_FLOAT32_VEC3, white);
  anariSetParameter(dev, mat, "metallic", ANARI_FLOAT32, &zero);
  anariSetParameter(dev, mat, "roughness", ANARI_FLOAT32, &one);
  anariCommitParameters(dev, mat);

  // Two quads, left (primitive 0) and right (primitive 1), sharing the middle
  // edge; st spans the unit square (u along +x, v along +y), so tangent T ~ +x
  // and tangent-space +X maps to world +x (screen right). attribute1 carries a
  // [-1,1] tangent-space normal per vertex: -X tilt on the left edge, flat in
  // the middle, +X tilt on the right edge.
  float verts[18] = {-0.9f,
      -0.9f,
      0.f,
      0.f,
      -0.9f,
      0.f,
      0.9f,
      -0.9f,
      0.f,
      -0.9f,
      0.9f,
      0.f,
      0.f,
      0.9f,
      0.f,
      0.9f,
      0.9f,
      0.f};
  float st[12] = {0.f, 0.f, 0.5f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.5f, 1.f, 1.f, 1.f};
  float packed[18];
  const float *perColumn[3] = {kTiltNeg, kFlat, kTiltPos};
  for (int v = 0; v < 6; ++v)
    std::copy(perColumn[v % 3], perColumn[v % 3] + 3, packed + 3 * v);
  uint32_t quads[8] = {0, 1, 4, 3, 1, 2, 5, 4};
  ANARIArray1D pos =
      anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 6);
  ANARIArray1D attr0 =
      anariNewArray1D(dev, st, nullptr, nullptr, ANARI_FLOAT32_VEC2, 6);
  ANARIArray1D attr1 =
      anariNewArray1D(dev, packed, nullptr, nullptr, ANARI_FLOAT32_VEC3, 6);
  // attribute2: the same tilts mirrored (+X on the left, -X on the right).
  float mirrored[18];
  const float *perColumnMirrored[3] = {kTiltPos, kFlat, kTiltNeg};
  for (int v = 0; v < 6; ++v)
    std::copy(perColumnMirrored[v % 3],
        perColumnMirrored[v % 3] + 3,
        mirrored + 3 * v);
  ANARIArray1D attr2 =
      anariNewArray1D(dev, mirrored, nullptr, nullptr, ANARI_FLOAT32_VEC3, 6);
  ANARIArray1D index =
      anariNewArray1D(dev, quads, nullptr, nullptr, ANARI_UINT32_VEC4, 2);
  ANARIGeometry g = anariNewGeometry(dev, "quad");
  anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
  anariSetParameter(dev, g, "vertex.attribute0", ANARI_ARRAY1D, &attr0);
  anariSetParameter(dev, g, "vertex.attribute1", ANARI_ARRAY1D, &attr1);
  anariSetParameter(dev, g, "vertex.attribute2", ANARI_ARRAY1D, &attr2);
  anariSetParameter(dev, g, "primitive.index", ANARI_ARRAY1D, &index);
  anariCommitParameters(dev, g);

  ANARISurface surf = anariNewSurface(dev);
  anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &g);
  anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
  anariCommitParameters(dev, surf);

  // A single directional light travelling toward -X/-Z (so it comes from the
  // +X, +Z side).
  ANARILight light = anariNewLight(dev, "directional");
  float ldir[3] = {-0.6f, 0.f, -0.8f};
  // Low enough that a +X tilt brightens without saturating 8-bit sRGB.
  float irr = 1.5f;
  anariSetParameter(dev, light, "direction", ANARI_FLOAT32_VEC3, ldir);
  anariSetParameter(dev, light, "irradiance", ANARI_FLOAT32, &irr);
  anariCommitParameters(dev, light);

  ANARIWorld world = anariNewWorld(dev);
  ANARIArray1D surfArray =
      anariNewArray1D(dev, &surf, nullptr, nullptr, ANARI_SURFACE, 1);
  ANARIArray1D lightArray =
      anariNewArray1D(dev, &light, nullptr, nullptr, ANARI_LIGHT, 1);
  anariSetParameter(dev, world, "surface", ANARI_ARRAY1D, &surfArray);
  anariSetParameter(dev, world, "light", ANARI_ARRAY1D, &lightArray);
  anariCommitParameters(dev, world);

  ANARICamera cam = anariNewCamera(dev, "perspective");
  float eye[3] = {0.f, 0.f, 3.f};
  float dir[3] = {0.f, 0.f, -1.f};
  float up[3] = {0.f, 1.f, 0.f};
  float fovy = 0.6f;
  anariSetParameter(dev, cam, "position", ANARI_FLOAT32_VEC3, eye);
  anariSetParameter(dev, cam, "direction", ANARI_FLOAT32_VEC3, dir);
  anariSetParameter(dev, cam, "up", ANARI_FLOAT32_VEC3, up);
  anariSetParameter(dev, cam, "fovy", ANARI_FLOAT32, &fovy);
  anariCommitParameters(dev, cam);

  ANARIRenderer ren = anariNewRenderer(dev, "default");
  anariCommitParameters(dev, ren);

  ANARIFrame frame = anariNewFrame(dev);
  uint32_t size[2] = {256, 256};
  ANARIDataType colorFmt = ANARI_UFIXED8_RGBA_SRGB;
  anariSetParameter(dev, frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(dev, frame, "channel.color", ANARI_DATA_TYPE, &colorFmt);
  anariSetParameter(dev, frame, "world", ANARI_WORLD, &world);
  anariSetParameter(dev, frame, "camera", ANARI_CAMERA, &cam);
  anariSetParameter(dev, frame, "renderer", ANARI_RENDERER, &ren);
  anariCommitParameters(dev, frame);

  auto bindNormal = [&](ANARISampler s) {
    if (s)
      anariSetParameter(dev, mat, "normal", ANARI_SAMPLER, &s);
    else
      anariUnsetParameter(dev, mat, "normal");
    anariCommitParameters(dev, mat);
  };
  const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  const float kZero4[4] = {0, 0, 0, 0};
  // A clear margin in 8-bit sRGB luma.
  const double kMargin = 25.0;

  // --- flat reference: no normal map -----------------------------------------
  std::printf("flat reference\n");
  const Halves flat = renderHalves(dev, frame, "no normal map");
  check(std::abs(flat.left - flat.right) < 5.0,
      "without a normal map both halves light equally");

  // --- image sampler ---------------------------------------------------------
  // The spec's decode: outTransform diag(2,2,2,1), outOffset (-1,-1,-1,0).
  const float kDecode[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1};
  std::printf("image sampler\n");
  ANARISampler img = anariNewSampler(dev, "image2D");
  anariSetParameter(dev, img, "image", ANARI_ARRAY2D, &image);
  anariSetParameter(dev, img, "wrapMode1", ANARI_STRING, "clampToEdge");
  anariSetParameter(dev, img, "wrapMode2", ANARI_STRING, "clampToEdge");
  setMat4(dev, img, "outTransform", kDecode);
  setVec4(dev, img, "outOffset", -1.f, -1.f, -1.f, 0.f);
  anariCommitParameters(dev, img);
  bindNormal(img);
  {
    const Halves h = renderHalves(dev, frame, "8-bit map, spec decode");
    check(h.right > h.left + kMargin,
        "the +X-tilted right half is brighter than the -X-tilted left half");
    // N.L goes 0.8 -> 1.0 toward the light but 0.8 -> 0.28 away from it, so
    // the brightening is the smaller effect.
    check(h.left < flat.left - kMargin && h.right > flat.right + 8.0,
        "the decoded tilts darken the left half and brighten the right");
  }
  {
    // The decode with an X flip folded in (the glTF/DirectX convention fix,
    // on the red channel since the light is along X): x' = -(2r - 1).
    const float decodeFlipX[16] = {
        -2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1};
    setMat4(dev, img, "outTransform", decodeFlipX);
    setVec4(dev, img, "outOffset", 1.f, -1.f, -1.f, 0.f);
    anariCommitParameters(dev, img);
    const Halves h = renderHalves(dev, frame, "8-bit map, decode with X flip");
    check(h.left > h.right + kMargin,
        "a diagonal outTransform flipping X swaps the bright half");
  }
  {
    // Channel mix x' = 2b - 2r - 0.8 (an off-diagonal term, so it is baked
    // into float texels): b = 0.9 on both texels, so this is x' = -(2r - 1)
    // again, and its negative values must survive the bake.
    const float mixDecodeFlipX[16] = {
        -2, 0, 0, 0, 0, 2, 0, 0, 2, 0, 2, 0, 0, 0, 0, 1};
    setMat4(dev, img, "outTransform", mixDecodeFlipX);
    setVec4(dev, img, "outOffset", -0.8f, -1.f, -1.f, 0.f);
    anariCommitParameters(dev, img);
    const Halves h =
        renderHalves(dev, frame, "8-bit map, channel-mixing decode + flip");
    check(h.left > h.right + kMargin,
        "a channel-mixing outTransform flipping X swaps the bright half");
  }
  {
    // A float map already in [-1,1] with identity outTransform is used as is.
    // Decoding it again would turn (-0.6,0,0.8) into (-2.2,-1,0.6).
    anariSetParameter(dev, img, "image", ANARI_ARRAY2D, &fimage);
    anariSetParameter(dev, img, "outTransform", ANARI_FLOAT32_MAT4, kIdentity);
    anariSetParameter(dev, img, "outOffset", ANARI_FLOAT32_VEC4, kZero4);
    anariCommitParameters(dev, img);
    const Halves h = renderHalves(dev, frame, "[-1,1] float map, identity");
    check(h.right > h.left + kMargin,
        "a pre-decoded map is not decoded again (right half brighter)");
    check(h.left < flat.left - kMargin && h.right > flat.right + 8.0,
        "a pre-decoded map darkens the left half and brightens the right");
  }
  {
    // A flat packed map: with the spec decode it renders like no normal map;
    // without it the sampler returns (0.5, 0.5, 1), a +X/+Y tilt the device
    // must not "fix" by decoding.
    anariSetParameter(dev, img, "image", ANARI_ARRAY2D, &flatImage);
    setMat4(dev, img, "outTransform", kDecode);
    setVec4(dev, img, "outOffset", -1.f, -1.f, -1.f, 0.f);
    anariCommitParameters(dev, img);
    const Halves d = renderHalves(dev, frame, "flat 8-bit map, spec decode");
    check(std::abs(d.left - flat.left) < 5.0
            && std::abs(d.right - flat.right) < 5.0,
        "a decoded flat map renders like the flat reference");
    anariSetParameter(dev, img, "outTransform", ANARI_FLOAT32_MAT4, kIdentity);
    anariSetParameter(dev, img, "outOffset", ANARI_FLOAT32_VEC4, kZero4);
    anariCommitParameters(dev, img);
    const Halves u = renderHalves(dev, frame, "flat 8-bit map, no decode");
    check(u.left > flat.left + 5.0 && u.right > flat.right + 5.0,
        "an undecoded flat map is not decoded by the device (both halves tilt "
        "toward the light)");
  }

  // --- transform and primitive samplers --------------------------------------
  // No texture: the bridge bakes their value into a derived primvar and, on the
  // normal slot, swaps Storm's UsdPreviewSurface for a generated variant whose
  // normal mapping reads that primvar as is and builds the tangent frame from
  // the geometry's `st` (SPEC §6.5). The attribute values are already in
  // [-1,1]. Same expectations as the image sampler.
  auto expectTilted = [&](const char *what) {
    const Halves h = renderHalves(dev, frame, what);
    check(h.right > h.left + kMargin,
        std::string(what) + ": the +X-tilted right half is brighter");
    check(h.left < flat.left - kMargin && h.right > flat.right + 8.0,
        std::string(what) + ": darkens the left half and brightens the right");
  };
  auto expectSwapped = [&](const char *what) {
    const Halves h = renderHalves(dev, frame, what);
    check(h.left > h.right + kMargin,
        std::string(what) + ": the bright half swaps to the left");
  };
  auto expectFlat = [&](const char *what) {
    const Halves h = renderHalves(dev, frame, what);
    check(std::abs(h.left - flat.left) < 5.0
            && std::abs(h.right - flat.right) < 5.0,
        std::string(what) + ": renders like the flat reference");
  };
  const float flipX[16] = {-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

  std::printf("transform sampler\n");
  ANARISampler xf = anariNewSampler(dev, "transform");
  anariSetParameter(dev, xf, "inAttribute", ANARI_STRING, "attribute1");
  anariCommitParameters(dev, xf);
  g_warnings = 0;
  bindNormal(xf);
  expectTilted("transform sampler on normal");
  check(
      g_warnings == 0, "a transform sampler on normal binds without a warning");
  // An outTransform edit re-bakes the primvar: x' = -x swaps the tilts.
  setMat4(dev, xf, "outTransform", flipX);
  anariCommitParameters(dev, xf);
  expectSwapped("transform outTransform x' = -x");
  anariSetParameter(dev, xf, "outTransform", ANARI_FLOAT32_MAT4, kIdentity);
  anariSetParameter(dev, xf, "outOffset", ANARI_FLOAT32_VEC4, kZero4);
  anariCommitParameters(dev, xf);
  expectTilted("transform outTransform back to identity");
  // An inAttribute edit: attribute2 carries the tilts mirrored.
  anariSetParameter(dev, xf, "inAttribute", ANARI_STRING, "attribute2");
  anariCommitParameters(dev, xf);
  expectSwapped("transform inAttribute -> mirrored attribute2");
  anariSetParameter(dev, xf, "inAttribute", ANARI_STRING, "attribute1");
  anariCommitParameters(dev, xf);
  expectTilted("transform inAttribute back to attribute1");

  // Without `st` there is no tangent frame (Storm builds it from st's screen
  // derivatives), so the map has no effect: the surface shades from its
  // geometric normal, as an image normal map on the same geometry does.
  anariUnsetParameter(dev, g, "vertex.attribute0");
  anariCommitParameters(dev, g);
  expectFlat("transform sampler, geometry without st");
  anariSetParameter(dev, g, "vertex.attribute0", ANARI_ARRAY1D, &attr0);
  anariCommitParameters(dev, g);
  expectTilted("transform sampler, st restored");

  // An instanced surface: the prototype rprim carries the same derived
  // primvar, so an identity instance renders the same.
  {
    ANARIGroup group = anariNewGroup(dev);
    anariSetParameter(dev, group, "surface", ANARI_ARRAY1D, &surfArray);
    anariCommitParameters(dev, group);
    ANARIInstance inst = anariNewInstance(dev, "transform");
    anariSetParameter(dev, inst, "group", ANARI_GROUP, &group);
    anariCommitParameters(dev, inst);
    ANARIArray1D instArray =
        anariNewArray1D(dev, &inst, nullptr, nullptr, ANARI_INSTANCE, 1);
    anariUnsetParameter(dev, world, "surface");
    anariSetParameter(dev, world, "instance", ANARI_ARRAY1D, &instArray);
    anariCommitParameters(dev, world);
    expectTilted("transform sampler, instanced surface");
    anariUnsetParameter(dev, world, "instance");
    anariSetParameter(dev, world, "surface", ANARI_ARRAY1D, &surfArray);
    anariCommitParameters(dev, world);
    anariRelease(dev, instArray);
    anariRelease(dev, inst);
    anariRelease(dev, group);
  }

  std::printf("primitive sampler\n");
  float perPrim[6] = {kTiltNeg[0],
      kTiltNeg[1],
      kTiltNeg[2],
      kTiltPos[0],
      kTiltPos[1],
      kTiltPos[2]};
  ANARIArray1D primValues =
      anariNewArray1D(dev, perPrim, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
  ANARISampler prim = anariNewSampler(dev, "primitive");
  anariSetParameter(dev, prim, "array", ANARI_ARRAY1D, &primValues);
  anariCommitParameters(dev, prim);
  g_warnings = 0;
  bindNormal(prim);
  expectTilted("primitive sampler on normal");
  check(
      g_warnings == 0, "a primitive sampler on normal binds without a warning");
  // A source-array edit: the per-primitive tilts swapped.
  float perPrimSwapped[6] = {kTiltPos[0],
      kTiltPos[1],
      kTiltPos[2],
      kTiltNeg[0],
      kTiltNeg[1],
      kTiltNeg[2]};
  ANARIArray1D primSwapped = anariNewArray1D(
      dev, perPrimSwapped, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
  anariSetParameter(dev, prim, "array", ANARI_ARRAY1D, &primSwapped);
  anariCommitParameters(dev, prim);
  expectSwapped("primitive array -> swapped values");

  bindNormal(nullptr);
  expectFlat("normal unbound again");

  anariRelease(dev, prim);
  anariRelease(dev, primSwapped);
  anariRelease(dev, primValues);
  anariRelease(dev, xf);
  anariRelease(dev, img);
  anariRelease(dev, frame);
  anariRelease(dev, ren);
  anariRelease(dev, cam);
  anariRelease(dev, lightArray);
  anariRelease(dev, light);
  anariRelease(dev, surfArray);
  anariRelease(dev, world);
  anariRelease(dev, surf);
  anariRelease(dev, g);
  anariRelease(dev, index);
  anariRelease(dev, attr2);
  anariRelease(dev, attr1);
  anariRelease(dev, attr0);
  anariRelease(dev, pos);
  anariRelease(dev, mat);
  anariRelease(dev, flatImage);
  anariRelease(dev, fimage);
  anariRelease(dev, image);

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
      g_errors);
  return g_errors == 0 ? 0 : 1;
}
