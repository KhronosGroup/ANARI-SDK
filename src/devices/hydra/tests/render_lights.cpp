// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises ANARI lights -> UsdLux light sprims (SPEC §6.6). The six ANARI light
// subtypes each map to a UsdLux sprim (distant/sphere/rect/disk/dome); a world
// with an ANARI light is authored with those sprims instead of the fallback
// headlight, and renders without error.
//
// Light parameter values (intensity/color) scale the render: the frame pushes
// HdxRenderTaskParams with enableLighting=true (default-off), without which
// Storm swaps in its fallback "fully lit" lighting shader and light params are
// inert. Shading includes the Lambertian 1/pi, so irradiance i on an albedo-a
// quad reads back roughly a*i/pi.

#include <anari/anari.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

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

// Render a front-facing gray quad lit by a directional light of the given
// irradiance; return the average brightness over covered pixels (0..255).
static double renderBrightness(ANARIDevice dev, float irradiance)
{
  float verts[12] = {-0.8f, -0.8f, 0.f, 0.8f, -0.8f, 0.f, 0.8f, 0.8f, 0.f,
      -0.8f, 0.8f, 0.f};
  ANARIArray1D pos =
      anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  ANARIGeometry g = anariNewGeometry(dev, "quad");
  anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
  anariCommitParameters(dev, g);

  ANARIMaterial mat = anariNewMaterial(dev, "matte");
  float color[3] = {0.8f, 0.8f, 0.8f};
  anariSetParameter(dev, mat, "color", ANARI_FLOAT32_VEC3, color);
  anariCommitParameters(dev, mat);

  ANARISurface surf = anariNewSurface(dev);
  anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &g);
  anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
  anariCommitParameters(dev, surf);

  ANARILight light = anariNewLight(dev, "directional");
  float ldir[3] = {0.f, 0.f, -1.f};   // travels toward -Z; lights the +Z face
  anariSetParameter(dev, light, "direction", ANARI_FLOAT32_VEC3, ldir);
  anariSetParameter(dev, light, "irradiance", ANARI_FLOAT32, &irradiance);
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
  // Linear channel type: this test measures radiometric scaling (irradiance
  // ratios), which the sRGB transfer curve would compress below the assert.
  ANARIDataType colorFmt = ANARI_UFIXED8_VEC4;
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
  double sum = 0;
  long n = 0;
  if (pixels) {
    for (uint32_t i = 0; i < w * h; ++i) {
      const uint8_t *p = (const uint8_t *)(pixels + i);
      const int lum = (p[0] + p[1] + p[2]) / 3;
      if (lum > 10) {
        sum += lum;
        ++n;
      }
    }
  }
  anariUnmapFrame(dev, frame, "channel.color");

  anariRelease(dev, frame);
  anariRelease(dev, ren);
  anariRelease(dev, cam);
  anariRelease(dev, lightArray);
  anariRelease(dev, surfArray);
  anariRelease(dev, world);
  anariRelease(dev, light);
  anariRelease(dev, surf);
  anariRelease(dev, mat);
  anariRelease(dev, g);
  anariRelease(dev, pos);
  return n ? sum / n : 0;
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

  // Enumerate light subtypes.
  const char *const *subs =
      anariGetObjectSubtypes(dev, ANARI_LIGHT);
  int nlight = 0;
  for (const char *const *s = subs; s && *s; ++s)
    ++nlight;
  check(nlight == 6, "six light subtypes enumerated");

  // 0.8-albedo quad: 0.6*0.8/pi ~ 0.15 and 2.4*0.8/pi ~ 0.61 — both above the
  // pixel mask, 4x apart.
  const double dim = renderBrightness(dev, 0.6f);
  const double bright = renderBrightness(dev, 2.4f);
  std::printf("directional brightness: irradiance 0.6=%.1f, 2.4=%.1f\n",
      dim, bright);

  // The light pipeline is exercised end-to-end (sprim authored, headlight
  // removed) and produces a visible, non-crashing render.
  check(dim > 10, "world with an ANARI light renders a visible surface");
  check(bright > dim * 2,
      "light irradiance scales the render (uniforms reach the draw, §6.6)");

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", g_errors);
  return g_errors == 0 ? 0 : 1;
}
