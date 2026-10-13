// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises the `default` renderer's core `background` (FLOAT32_VEC4) becoming
// the color the color buffer clears to where primary rays miss geometry — a true
// clear color (SPEC §4.7). It is honored whenever no dome paints the backdrop.
//
// A dome light OWNS the backdrop under Storm: HdxSkydomeTask is the first render
// task and paints the dome environment into primary rays, clobbering any clear
// value. The bridge suppresses that skydome via the global Hydra render setting
// HdRenderSettingsTokens->domeLightCameraVisibility, following this policy:
//   - no hdri dome present -> false (so the synthetic ambient fill dome, which is
//     never an hdri, is always camera-invisible and `background` owns the backdrop)
//   - an hdri dome present -> follow the first hdri's `visible` (default true), so
//     a default hdri stays the backdrop while `visible=false` reveals `background`.
//
// Four cases, all with sharp RELATIVE assertions (no brittle absolute thresholds):
//   1. no dome:            blue background shows in the corners, quad present.
//   2. ambient + blue bg:  corner STILL blue (ambient dome is camera-invisible)
//                          while the quad is lit by the ambient fill. The crux.
//   3. hdri `visible`:     a red hdri with visible(default/true) paints the corner
//                          red (env is the backdrop); visible=false reveals the
//                          blue background while the quad stays lit by the hdri.
//   4. bg change:          render blue then green with no channel-set change; the
//                          backdrop follows (guards the SetRenderOutputs guard trap).

#include <anari/anari.h>

#include <cstdint>
#include <cstdio>
#include <vector>

static int g_errors = 0;

static void statusFunc(const void *, ANARIDevice, ANARIObject, ANARIDataType,
    ANARIStatusSeverity severity, ANARIStatusCode, const char *message)
{
  if (severity <= ANARI_SEVERITY_WARNING)
    std::printf("[%s] %s\n",
        severity <= ANARI_SEVERITY_ERROR ? "ERR" : "WARN", message);
  if (severity <= ANARI_SEVERITY_ERROR)
    ++g_errors;
}

static void check(bool ok, const char *what)
{
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok)
    ++g_errors;
}

// Average the RGB (0..255) over a `size`x`size` pixel box anchored at (x0,y0),
// clamped to the image. Shared by the corner and center measurements.
static void avgBox(const uint8_t *pixels, uint32_t w, uint32_t h, uint32_t x0,
    uint32_t y0, uint32_t size, double out[3])
{
  out[0] = out[1] = out[2] = 0.0;
  double s[3] = {0, 0, 0};
  long n = 0;
  for (uint32_t y = y0; y < y0 + size && y < h; ++y) {
    for (uint32_t x = x0; x < x0 + size && x < w; ++x) {
      const uint8_t *p = pixels + (size_t(y) * w + x) * 4;
      s[0] += p[0];
      s[1] += p[1];
      s[2] += p[2];
      ++n;
    }
  }
  if (n) {
    out[0] = s[0] / n;
    out[1] = s[1] / n;
    out[2] = s[2] / n;
  }
}

// hdri modes for the scene builder.
enum HdriMode
{
  HDRI_NONE = 0,       // no hdri light
  HDRI_VISIBLE = 1,    // red hdri, `visible` left default (true)
  HDRI_INVISIBLE = 2,  // red hdri, `visible` = false
};

// Render a small centered white matte quad against `bg` clear color, optionally
// with an ambient fill and/or a red hdri dome. Returns the average corner color
// (top-left box, always background) and the average center color (the quad).
static void renderScene(ANARIDevice dev, const float bg[4],
    float ambientRadiance, HdriMode hdri, double corner[3], double center[3])
{
  corner[0] = corner[1] = corner[2] = 0.0;
  center[0] = center[1] = center[2] = 0.0;

  // A small white matte quad at z=0, centered and facing +Z (toward the camera).
  // Small enough (±0.4) that the image corners are background, not quad.
  float verts[12] = {-0.4f, -0.4f, 0.f, 0.4f, -0.4f, 0.f, 0.4f, 0.4f, 0.f,
      -0.4f, 0.4f, 0.f};
  ANARIArray1D pos =
      anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  ANARIGeometry g = anariNewGeometry(dev, "quad");
  anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
  anariCommitParameters(dev, g);

  ANARIMaterial mat = anariNewMaterial(dev, "matte");
  float white[3] = {1.f, 1.f, 1.f};
  anariSetParameter(dev, mat, "color", ANARI_FLOAT32_VEC3, white);
  anariCommitParameters(dev, mat);

  ANARISurface surf = anariNewSurface(dev);
  anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &g);
  anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
  anariCommitParameters(dev, surf);

  ANARIWorld world = anariNewWorld(dev);
  ANARIArray1D surfArray =
      anariNewArray1D(dev, &surf, nullptr, nullptr, ANARI_SURFACE, 1);
  anariSetParameter(dev, world, "surface", ANARI_ARRAY1D, &surfArray);

  // Optional red hdri dome. A solid 8x8 red equirectangular environment; the
  // dome both lights the quad red and (when visible) paints the backdrop red.
  ANARILight light = nullptr;
  ANARIArray1D lightArray = nullptr;
  ANARIArray2D radiance = nullptr;
  // Shared array memory: must outlive the array (the device may decode it as
  // late as anariRenderFrame, when deferred commits are applied).
  const int W = 8, H = 8;
  std::vector<float> env;
  if (hdri != HDRI_NONE) {
    env.resize(size_t(W) * H * 3);
    for (int i = 0; i < W * H; ++i) {
      env[i * 3 + 0] = 1.f;
      env[i * 3 + 1] = 0.f;
      env[i * 3 + 2] = 0.f;
    }
    radiance = anariNewArray2D(
        dev, env.data(), nullptr, nullptr, ANARI_FLOAT32_VEC3, W, H);
    light = anariNewLight(dev, "hdri");
    anariSetParameter(dev, light, "radiance", ANARI_ARRAY2D, &radiance);
    float scale = 1.f;
    anariSetParameter(dev, light, "scale", ANARI_FLOAT32, &scale);
    if (hdri == HDRI_INVISIBLE) {
      uint32_t vis = 0;  // ANARI_BOOL is a 32-bit int
      anariSetParameter(dev, light, "visible", ANARI_BOOL, &vis);
    }
    anariCommitParameters(dev, light);
    lightArray =
        anariNewArray1D(dev, &light, nullptr, nullptr, ANARI_LIGHT, 1);
    anariSetParameter(dev, world, "light", ANARI_ARRAY1D, &lightArray);
  }
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
  anariSetParameter(dev, ren, "background", ANARI_FLOAT32_VEC4, bg);
  anariSetParameter(
      dev, ren, "ambientRadiance", ANARI_FLOAT32, &ambientRadiance);
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

  anariRenderFrame(dev, frame);
  anariFrameReady(dev, frame, ANARI_WAIT);

  uint32_t w = 0, h = 0;
  ANARIDataType pt = ANARI_UNKNOWN;
  const uint8_t *pixels =
      (const uint8_t *)anariMapFrame(dev, frame, "channel.color", &w, &h, &pt);
  if (pixels) {
    // Corner: top-left 16x16 box, always background (the quad is centered).
    avgBox(pixels, w, h, 0, 0, 16, corner);
    // Center: central 24x24 box, squarely inside the quad.
    avgBox(pixels, w, h, w / 2 - 12, h / 2 - 12, 24, center);
  }
  anariUnmapFrame(dev, frame, "channel.color");

  anariRelease(dev, frame);
  anariRelease(dev, ren);
  anariRelease(dev, cam);
  if (lightArray)
    anariRelease(dev, lightArray);
  anariRelease(dev, surfArray);
  anariRelease(dev, world);
  anariRelease(dev, surf);
  anariRelease(dev, mat);
  anariRelease(dev, g);
  anariRelease(dev, pos);
  if (light)
    anariRelease(dev, light);
  if (radiance)
    anariRelease(dev, radiance);
}

// Case 4: render two backgrounds through ONE frame so the channel set never
// changes between them (the SetRenderOutputs guard would hide a clear value
// wired behind it). Returns the two corner colors.
static void renderTwoBackgrounds(ANARIDevice dev, const float bg0[4],
    const float bg1[4], double corner0[3], double corner1[3])
{
  float verts[12] = {-0.4f, -0.4f, 0.f, 0.4f, -0.4f, 0.f, 0.4f, 0.4f, 0.f,
      -0.4f, 0.4f, 0.f};
  ANARIArray1D pos =
      anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  ANARIGeometry g = anariNewGeometry(dev, "quad");
  anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
  anariCommitParameters(dev, g);

  ANARIMaterial mat = anariNewMaterial(dev, "matte");
  float white[3] = {1.f, 1.f, 1.f};
  anariSetParameter(dev, mat, "color", ANARI_FLOAT32_VEC3, white);
  anariCommitParameters(dev, mat);

  ANARISurface surf = anariNewSurface(dev);
  anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &g);
  anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
  anariCommitParameters(dev, surf);

  ANARIWorld world = anariNewWorld(dev);
  ANARIArray1D surfArray =
      anariNewArray1D(dev, &surf, nullptr, nullptr, ANARI_SURFACE, 1);
  anariSetParameter(dev, world, "surface", ANARI_ARRAY1D, &surfArray);
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
  anariSetParameter(dev, ren, "background", ANARI_FLOAT32_VEC4, bg0);
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

  auto sampleCorner = [&](double corner[3]) {
    corner[0] = corner[1] = corner[2] = 0.0;
    anariRenderFrame(dev, frame);
    anariFrameReady(dev, frame, ANARI_WAIT);
    uint32_t w = 0, h = 0;
    ANARIDataType pt = ANARI_UNKNOWN;
    const uint8_t *pixels = (const uint8_t *)anariMapFrame(
        dev, frame, "channel.color", &w, &h, &pt);
    if (pixels)
      avgBox(pixels, w, h, 0, 0, 16, corner);
    anariUnmapFrame(dev, frame, "channel.color");
  };

  sampleCorner(corner0);

  // Change ONLY the background and re-render through the same frame.
  anariSetParameter(dev, ren, "background", ANARI_FLOAT32_VEC4, bg1);
  anariCommitParameters(dev, ren);
  sampleCorner(corner1);

  anariRelease(dev, frame);
  anariRelease(dev, ren);
  anariRelease(dev, cam);
  anariRelease(dev, surfArray);
  anariRelease(dev, world);
  anariRelease(dev, surf);
  anariRelease(dev, mat);
  anariRelease(dev, g);
  anariRelease(dev, pos);
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

  const float blue[4] = {0.f, 0.f, 1.f, 1.f};
  const float green[4] = {0.f, 1.f, 0.f, 1.f};

  // Case 1 — no dome: blue background shows in the corners; quad present.
  double c1corner[3], c1center[3];
  renderScene(dev, blue, 0.f, HDRI_NONE, c1corner, c1center);
  std::printf("case1 no-dome:   corner=%.0f %.0f %.0f  center=%.0f %.0f %.0f\n",
      c1corner[0], c1corner[1], c1corner[2], c1center[0], c1center[1],
      c1center[2]);
  check(c1corner[2] > c1corner[0] && c1corner[2] > c1corner[1]
          && c1corner[2] > 60.0,
      "case1: background corner is blue-dominant and clearly non-black");
  check(c1center[0] + c1center[1] + c1center[2] > 30.0,
      "case1: the centered quad is present (lit by the fallback headlight)");

  // Case 2 (the crux) — ambient dome + blue background: the ambient fill dome is
  // camera-invisible per policy, so the corner stays blue while the quad lifts.
  double c2corner[3], c2center[3];
  renderScene(dev, blue, 2.f, HDRI_NONE, c2corner, c2center);
  std::printf("case2 ambient:   corner=%.0f %.0f %.0f  center=%.0f %.0f %.0f\n",
      c2corner[0], c2corner[1], c2corner[2], c2center[0], c2center[1],
      c2center[2]);
  check(c2corner[2] > c2corner[0] && c2corner[2] > c2corner[1]
          && c2corner[2] > 60.0,
      "case2: corner is STILL blue with an ambient dome (dome camera-invisible)");
  check(c2center[0] + c2center[1] + c2center[2] > 30.0,
      "case2: the quad is lit by the ambient fill");

  // Case 3 — hdri `visible` drives the backdrop. Default/visible: red corner
  // (env is the backdrop). visible=false: blue corner while the quad stays lit.
  double c3vCorner[3], c3vCenter[3];
  renderScene(dev, blue, 0.f, HDRI_VISIBLE, c3vCorner, c3vCenter);
  double c3iCorner[3], c3iCenter[3];
  renderScene(dev, blue, 0.f, HDRI_INVISIBLE, c3iCorner, c3iCenter);
  std::printf(
      "case3 hdri vis:  corner=%.0f %.0f %.0f  invis: corner=%.0f %.0f %.0f\n",
      c3vCorner[0], c3vCorner[1], c3vCorner[2], c3iCorner[0], c3iCorner[1],
      c3iCorner[2]);
  std::printf("case3 hdri centers: vis=%.0f %.0f %.0f  invis=%.0f %.0f %.0f\n",
      c3vCenter[0], c3vCenter[1], c3vCenter[2], c3iCenter[0], c3iCenter[1],
      c3iCenter[2]);
  check(c3vCorner[0] > c3vCorner[2] && c3vCorner[0] > 60.0,
      "case3: a default (visible) hdri paints the corner red (env is backdrop)");
  check(c3iCorner[2] > c3iCorner[0] && c3iCorner[2] > 60.0,
      "case3: hdri visible=false reveals the blue background in the corner");
  check(c3iCenter[0] > c3iCenter[2] && c3iCenter[0] > 30.0,
      "case3: the quad stays lit by the hdri even when it is camera-invisible");

  // Case 4 — background changes without a channel-set change: blue then green.
  double c4corner0[3], c4corner1[3];
  renderTwoBackgrounds(dev, blue, green, c4corner0, c4corner1);
  std::printf("case4 bg-change: blue=%.0f %.0f %.0f  green=%.0f %.0f %.0f\n",
      c4corner0[0], c4corner0[1], c4corner0[2], c4corner1[0], c4corner1[1],
      c4corner1[2]);
  check(c4corner0[2] > c4corner0[0] && c4corner0[2] > c4corner0[1],
      "case4: first render's corner is blue");
  check(c4corner1[1] > c4corner1[0] && c4corner1[1] > c4corner1[2],
      "case4: after changing only `background`, the corner follows to green");

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", g_errors);
  return g_errors == 0 ? 0 : 1;
}
