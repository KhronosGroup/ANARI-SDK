// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises the material path (SPEC §6.1/§6.5/§6.9). Each material authors a
// UsdPreviewSurface network into the mirror AND the constant-color displayColor
// fallback; the device binds the network sprim to the rprim. This test asserts
// the observable, spec-guaranteed result: each surface renders with a color that
// tracks its material's constant color (the §6.9 fallback path, honored by both
// Storm and Embree).
//
// Network-only effects (emissiveColor) are also asserted: the frame pushes
// HdxRenderTaskParams with enableLighting=true (default-off), without which
// Storm swaps in its fallback lighting shader and the network's constant
// params never reach the mesh draw.

#include <anari/anari.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

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

static void check(bool ok, const char *what)
{
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok)
    ++g_errors;
}

// Render one full-screen-ish quad with `mat` and return the average lit color
// over covered pixels (RGB 0..255).
static void renderAvgColor(
    ANARIDevice dev, ANARIMaterial mat, double avg[3])
{
  float verts[12] = {-0.9f, -0.9f, 0.f, 0.9f, -0.9f, 0.f, 0.9f, 0.9f, 0.f,
      -0.9f, 0.9f, 0.f};
  ANARIArray1D pos =
      anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  ANARIGeometry g = anariNewGeometry(dev, "quad");
  anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
  anariCommitParameters(dev, g);

  ANARISurface surf = anariNewSurface(dev);
  anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &g);
  anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
  anariCommitParameters(dev, surf);

  ANARIWorld world = anariNewWorld(dev);
  ANARIArray1D surfArray =
      anariNewArray1D(dev, &surf, nullptr, nullptr, ANARI_SURFACE, 1);
  anariSetParameter(dev, world, "surface", ANARI_ARRAY1D, &surfArray);
  anariCommitParameters(dev, world);   // lit by the Frame headlight fallback

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

  anariRenderFrame(dev, frame);
  anariFrameReady(dev, frame, ANARI_WAIT);

  uint32_t w = 0, h = 0;
  ANARIDataType pt = ANARI_UNKNOWN;
  const uint32_t *pixels = (const uint32_t *)anariMapFrame(
      dev, frame, "channel.color", &w, &h, &pt);
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
  anariUnmapFrame(dev, frame, "channel.color");

  anariRelease(dev, frame);
  anariRelease(dev, ren);
  anariRelease(dev, cam);
  anariRelease(dev, surfArray);
  anariRelease(dev, world);
  anariRelease(dev, surf);
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

  // A red matte surface: the shaded result must be red-dominant (network
  // authored diffuseColor reached Storm).
  {
    ANARIMaterial mat = anariNewMaterial(dev, "matte");
    float color[3] = {0.85f, 0.1f, 0.1f};
    anariSetParameter(dev, mat, "color", ANARI_FLOAT32_VEC3, color);
    anariCommitParameters(dev, mat);
    double avg[3];
    renderAvgColor(dev, mat, avg);
    std::printf("matte(red) avg rgb: %.0f %.0f %.0f\n", avg[0], avg[1], avg[2]);
    check(avg[0] > 60, "matte surface renders lit");
    check(avg[0] > avg[1] + 25 && avg[0] > avg[2] + 25,
        "matte color reaches the delegate (red-dominant, §6.9)");
    anariRelease(dev, mat);
  }

  // A blue physicallyBased surface: blue-dominant confirms baseColor mapping.
  {
    ANARIMaterial mat = anariNewMaterial(dev, "physicallyBased");
    float color[3] = {0.1f, 0.1f, 0.85f};
    float metallic = 0.f;
    float roughness = 0.6f;
    anariSetParameter(dev, mat, "baseColor", ANARI_FLOAT32_VEC3, color);
    anariSetParameter(dev, mat, "metallic", ANARI_FLOAT32, &metallic);
    anariSetParameter(dev, mat, "roughness", ANARI_FLOAT32, &roughness);
    anariCommitParameters(dev, mat);
    double avg[3];
    renderAvgColor(dev, mat, avg);
    std::printf("pbr(blue) avg rgb: %.0f %.0f %.0f\n", avg[0], avg[1], avg[2]);
    check(avg[2] > 60, "physicallyBased surface renders lit");
    check(avg[2] > avg[0] + 25 && avg[2] > avg[1] + 25,
        "physicallyBased baseColor reaches the delegate (blue-dominant, §6.9)");
    anariRelease(dev, mat);
  }

  // emissiveColor has no displayColor equivalent: a black-base emissive-green
  // surface glows green only if the network's constant params are bound to the
  // draw — the network-only path, asserted.
  {
    ANARIMaterial mat = anariNewMaterial(dev, "physicallyBased");
    float base[3] = {0.f, 0.f, 0.f};
    float emissive[3] = {0.f, 0.8f, 0.f};
    anariSetParameter(dev, mat, "baseColor", ANARI_FLOAT32_VEC3, base);
    anariSetParameter(dev, mat, "emissive", ANARI_FLOAT32_VEC3, emissive);
    anariCommitParameters(dev, mat);
    double avg[3];
    renderAvgColor(dev, mat, avg);
    std::printf("pbr(emissive-green,black-base) avg rgb: %.0f %.0f %.0f\n",
        avg[0], avg[1], avg[2]);
    check(avg[1] > 60, "emissive surface glows on a black base");
    check(avg[1] > avg[0] + 25 && avg[1] > avg[2] + 25,
        "emissiveColor reaches the mesh draw (green-dominant, network-only)");
    anariRelease(dev, mat);
  }

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", g_errors);
  return g_errors == 0 ? 0 : 1;
}
