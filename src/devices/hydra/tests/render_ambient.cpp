// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises the `default` renderer's ambientRadiance/ambientColor injecting a
// flat, shadowless ambient fill. The device synthesizes one uniform dome-light
// sprim (a 1x1 env texture of ambientColor * ambientRadiance) into a
// frame-owned scene index whenever ambientRadiance > 0. Scene-ambient via
// GlfSimpleLightingContext does NOT reach UsdPreviewSurface under Storm (proto
// 016), hence a real dome-light sprim.
//
// Setup: a white physicallyBased quad faces the camera, lit by ONE directional
// light aimed so it MISSES the front face (direction (1,0,0) grazes the quad's
// plane). The front is therefore dark without ambient.
// Render twice — ambientRadiance 0 (front dark) vs ambientRadiance 2 (front
// bright) — and assert a large, relative lift. A red ambientColor tints the
// lift red, and in a float frame doubling ambientRadiance doubles the fill.
// A final case proves the ambient headlight drop: a world with NO lights and
// ambientRadiance > 0 still renders visible (the ambient fill stands alone).

#include <anari/anari.h>

#include <cstdint>
#include <cstdio>
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

// Render a white physicallyBased quad facing the camera and measure the average
// RGB over the central region (the front face). `withBackLight` adds a single
// directional light travelling +Z (away from the camera) so it lights the back
// only. `ambientRadiance`/`ambientColor` drive the renderer's ambient fill.
// Averages over a fixed central box regardless of brightness, so a dark front
// face reads back near-zero rather than being filtered out.
static void renderFrontFace(ANARIDevice dev,
    bool withBackLight,
    float ambientRadiance,
    const float ambientColor[3],
    double out[3],
    bool floatFrame = false)
{
  out[0] = out[1] = out[2] = 0.0;

  // A white matte physicallyBased quad at z=0, facing +Z (toward the camera).
  float verts[12] = {
      -0.8f, -0.8f, 0.f, 0.8f, -0.8f, 0.f, 0.8f, 0.8f, 0.f, -0.8f, 0.8f, 0.f};
  ANARIArray1D pos =
      anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  ANARIGeometry g = anariNewGeometry(dev, "quad");
  anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
  anariCommitParameters(dev, g);

  ANARIMaterial mat = anariNewMaterial(dev, "physicallyBased");
  float white[3] = {1.f, 1.f, 1.f};
  float zero = 0.f, one = 1.f;
  anariSetParameter(dev, mat, "baseColor", ANARI_FLOAT32_VEC3, white);
  anariSetParameter(dev, mat, "metallic", ANARI_FLOAT32, &zero);
  anariSetParameter(dev, mat, "roughness", ANARI_FLOAT32, &one);
  anariCommitParameters(dev, mat);

  ANARISurface surf = anariNewSurface(dev);
  anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &g);
  anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
  anariCommitParameters(dev, surf);

  ANARIWorld world = anariNewWorld(dev);
  ANARIArray1D surfArray =
      anariNewArray1D(dev, &surf, nullptr, nullptr, ANARI_SURFACE, 1);
  anariSetParameter(dev, world, "surface", ANARI_ARRAY1D, &surfArray);

  // A directional light travelling along the quad's plane (+X) grazes it: the
  // front (facing the camera) gets no direct light, so any front brightness is
  // ambient alone. Grazing rather than from behind, since a delegate that
  // lights both sides of a mesh (HdEmbree) would light the front from behind.
  ANARILight light = nullptr;
  ANARIArray1D lightArray = nullptr;
  if (withBackLight) {
    light = anariNewLight(dev, "directional");
    float ldir[3] = {1.f, 0.f, 0.f};
    float irr = 3.f;
    anariSetParameter(dev, light, "direction", ANARI_FLOAT32_VEC3, ldir);
    anariSetParameter(dev, light, "irradiance", ANARI_FLOAT32, &irr);
    anariCommitParameters(dev, light);
    lightArray = anariNewArray1D(dev, &light, nullptr, nullptr, ANARI_LIGHT, 1);
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
  anariSetParameter(
      dev, ren, "ambientRadiance", ANARI_FLOAT32, &ambientRadiance);
  anariSetParameter(dev, ren, "ambientColor", ANARI_FLOAT32_VEC3, ambientColor);
  anariCommitParameters(dev, ren);

  ANARIFrame frame = anariNewFrame(dev);
  uint32_t size[2] = {256, 256};
  ANARIDataType colorFmt =
      floatFrame ? ANARI_FLOAT32_VEC4 : ANARI_UFIXED8_RGBA_SRGB;
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
  const void *pixels = anariMapFrame(dev, frame, "channel.color", &w, &h, &pt);
  double s[3] = {0, 0, 0};
  long n = 0;
  if (pixels) {
    // Average over the central quarter — squarely inside the front face, so the
    // measurement is the front-face color whether it is dark or bright.
    const uint32_t x0 = w / 4, x1 = 3 * w / 4;
    const uint32_t y0 = h / 4, y1 = 3 * h / 4;
    for (uint32_t y = y0; y < y1; ++y) {
      for (uint32_t x = x0; x < x1; ++x) {
        const size_t i = (size_t(y) * w + x) * 4;
        for (int k = 0; k < 3; ++k)
          s[k] += pt == ANARI_FLOAT32_VEC4
              ? double(static_cast<const float *>(pixels)[i + k])
              : double(static_cast<const uint8_t *>(pixels)[i + k]);
        ++n;
      }
    }
  }
  anariUnmapFrame(dev, frame, "channel.color");
  if (n) {
    out[0] = s[0] / n;
    out[1] = s[1] / n;
    out[2] = s[2] / n;
  }

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
}

static double luma(const double rgb[3])
{
  return 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
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

  const float white[3] = {1.f, 1.f, 1.f};
  const float red[3] = {1.f, 0.f, 0.f};

  // Back-lit quad, no ambient -> the camera-facing front is dark.
  double dark[3];
  renderFrontFace(dev, /*withBackLight=*/true, 0.f, white, dark);

  // Same scene, ambientRadiance 2 -> the front face lifts.
  double bright[3];
  renderFrontFace(dev, /*withBackLight=*/true, 2.f, white, bright);

  // Same scene, red ambient -> the lift is red-dominant.
  double redLit[3];
  renderFrontFace(dev, /*withBackLight=*/true, 2.f, red, redLit);

  // No lights at all + ambient -> still visible (ambient headlight drop).
  double ambientOnly[3];
  renderFrontFace(dev, /*withBackLight=*/false, 2.f, white, ambientOnly);

  // Linear (float frame) fill at ambientRadiance 0.25 and 0.5: the radiance
  // scales the fill on every delegate.
  double half[3], quarter[3];
  renderFrontFace(dev, /*withBackLight=*/true, 0.25f, white, quarter, true);
  renderFrontFace(dev, /*withBackLight=*/true, 0.5f, white, half, true);

  std::printf(
      "front-face luma:  dark=%.1f  bright=%.1f\n", luma(dark), luma(bright));
  std::printf(
      "red-ambient front = %.1f %.1f %.1f\n", redLit[0], redLit[1], redLit[2]);
  std::printf("ambient-only (no lights) luma = %.1f\n", luma(ambientOnly));

  check(luma(dark) < 30.0, "front face is dark without ambient");
  check(luma(bright) > luma(dark) + 40.0,
      "ambientRadiance clearly lifts the unlit front face");
  check(luma(bright) > 2.0 * luma(dark) + 1.0,
      "the lift is large and relative (bright >> dark)");
  check(redLit[0] > redLit[1] && redLit[0] > redLit[2],
      "ambientColor red tints the fill red");
  std::printf("linear fill: radiance 0.25 -> %.4f, 0.5 -> %.4f\n",
      luma(quarter),
      luma(half));
  check(luma(quarter) > 0.01 && luma(half) > 1.9 * luma(quarter)
          && luma(half) < 2.1 * luma(quarter),
      "ambientRadiance scales the fill linearly");
  check(luma(ambientOnly) > 40.0,
      "ambient-only world (no lights, headlight dropped) still renders visible");

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
      g_errors);
  return g_errors == 0 ? 0 : 1;
}
