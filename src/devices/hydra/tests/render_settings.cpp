// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises the renderer-settings push. A renderer parameter named with a
// delegate's verbatim Hydra render-setting token must reach the frame's
// delegate via SetRenderSetting. HdEmbree is the observable target: its
// `enableSceneColors` setting toggles whether surfaces shade from their display
// color. Rendering the same red quad with the setting false vs true must
// produce a visibly different result — proving the setting reached the delegate
// (a sharp check, not "runs without error").

#include <anari/anari.h>

#include <cstdint>
#include <cstdio>

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

// Render a red quad through `dev`; if `setSceneColors` is true, set the embree
// `enableSceneColors` render setting to `sceneColors`, and if `pixelSamples` is
// non-null set the renderer's `pixelSamples` to it. Returns the average lit
// color over covered pixels and, optionally, the covered pixel count.
static void renderRedQuad(ANARIDevice dev,
    bool setSceneColors,
    bool sceneColors,
    double avg[3],
    const int32_t *pixelSamples = nullptr,
    long *coverage = nullptr)
{
  float verts[12] = {
      -0.9f, -0.9f, 0.f, 0.9f, -0.9f, 0.f, 0.9f, 0.9f, 0.f, -0.9f, 0.9f, 0.f};
  ANARIArray1D pos =
      anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  ANARIGeometry g = anariNewGeometry(dev, "quad");
  anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
  anariCommitParameters(dev, g);

  ANARIMaterial mat = anariNewMaterial(dev, "matte");
  float color[3] = {0.9f, 0.05f, 0.05f};
  anariSetParameter(dev, mat, "color", ANARI_FLOAT32_VEC3, color);
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
  if (setSceneColors) {
    uint32_t v = sceneColors ? 1u : 0u;
    anariSetParameter(dev, ren, "enableSceneColors", ANARI_BOOL, &v);
  }
  if (pixelSamples)
    anariSetParameter(dev, ren, "pixelSamples", ANARI_INT32, pixelSamples);
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
  const uint32_t *pixels =
      (const uint32_t *)anariMapFrame(dev, frame, "channel.color", &w, &h, &pt);
  double sum[3] = {0, 0, 0};
  long n = 0;
  if (pixels) {
    for (uint32_t i = 0; i < w * h; ++i) {
      const uint8_t *p = (const uint8_t *)(pixels + i);
      if (p[0] > 20 || p[1] > 20 || p[2] > 20) {
        sum[0] += p[0];
        sum[1] += p[1];
        sum[2] += p[2];
        ++n;
      }
    }
  }
  avg[0] = n ? sum[0] / n : 0;
  avg[1] = n ? sum[1] / n : 0;
  avg[2] = n ? sum[2] / n : 0;
  if (coverage)
    *coverage = n;
  anariUnmapFrame(dev, frame, "channel.color");

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
  const char *subtype = argc > 1 ? argv[1] : "embree";

  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  if (!lib)
    return 2;
  ANARIDevice dev = anariNewDevice(lib, subtype);
  if (!dev)
    return 2;

  // With scene colors ON the red quad shades red; with scene colors OFF embree
  // ignores the display color and the red no longer dominates. The two renders
  // differing proves the render setting reached the delegate.
  double on[3], off[3];
  renderRedQuad(dev, true, true, on);
  renderRedQuad(dev, true, false, off);
  std::printf(
      "enableSceneColors=1 avg rgb: %.0f %.0f %.0f\n", on[0], on[1], on[2]);
  std::printf(
      "enableSceneColors=0 avg rgb: %.0f %.0f %.0f\n", off[0], off[1], off[2]);

  check(on[0] > on[1] + 30 && on[0] > on[2] + 30,
      "enableSceneColors=1: red quad shades red");
  const bool offNotRed = !(off[0] > off[1] + 30 && off[0] > off[2] + 30);
  check(offNotRed,
      "enableSceneColors=0: red no longer dominates (setting reached delegate)");

  // The sample budget: unset means the advertised default of 1, and a zero
  // budget (what an app zero-filling an introspected parameter sends) falls
  // back to 1 — HdEmbree would otherwise report converged on an empty frame
  // showing only the background.
  const int32_t zeroSamples = 0;
  long unsetCoverage = 0, zeroCoverage = 0;
  double scratch[3];
  renderRedQuad(dev, false, false, scratch, nullptr, &unsetCoverage);
  renderRedQuad(dev, false, false, scratch, &zeroSamples, &zeroCoverage);
  std::printf("coverage: pixelSamples unset %ld, pixelSamples=0 %ld\n",
      unsetCoverage,
      zeroCoverage);
  check(unsetCoverage > 10000, "pixelSamples unset: the quad renders");
  check(zeroCoverage > 10000,
      "pixelSamples=0: the quad still renders (falls back to 1)");

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
      g_errors);
  return g_errors == 0 ? 0 : 1;
}
