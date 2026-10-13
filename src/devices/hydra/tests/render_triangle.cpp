// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// End-to-end smoke test: drive the anari-hydra forward device through the ANARI
// C API to render a single triangle via a Hydra delegate (default: storm) and
// read the frame back. Verifies the whole bridge: library -> device subtype ->
// object graph -> zero-copy mirror -> Storm render -> frame map.

#include <anari/anari.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
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

int main(int argc, char **argv)
{
  const char *subtype = argc > 1 ? argv[1] : "storm";

  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  if (!lib) {
    std::printf("FAIL: could not load anari library 'hydra'\n");
    return 2;
  }

  // Enumerate device subtypes (one per Hydra delegate).
  const char *const *subtypes = anariGetDeviceSubtypes(lib);
  std::printf("device subtypes:");
  bool foundSubtype = false;
  for (const char *const *s = subtypes; s && *s; ++s) {
    std::printf(" %s", *s);
    if (std::strcmp(*s, subtype) == 0)
      foundSubtype = true;
  }
  std::printf("\n");
  check(foundSubtype, "requested delegate subtype is enumerated");

  ANARIDevice dev = anariNewDevice(lib, subtype);
  check(dev != nullptr, "device created for delegate subtype");
  if (!dev)
    return 2;

  // --- geometry: one triangle -----------------------------------------------
  float verts[9] = {-0.8f, -0.8f, 0.0f, 0.8f, -0.8f, 0.0f, 0.0f, 0.9f, 0.0f};

  ANARIArray1D posArray =
      anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 3);

  ANARIGeometry geom = anariNewGeometry(dev, "triangle");
  anariSetParameter(dev, geom, "vertex.position", ANARI_ARRAY1D, &posArray);
  anariCommitParameters(dev, geom);

  ANARIMaterial mat = anariNewMaterial(dev, "matte");
  float color[3] = {0.9f, 0.5f, 0.1f};
  anariSetParameter(dev, mat, "color", ANARI_FLOAT32_VEC3, color);
  anariCommitParameters(dev, mat);

  ANARISurface surf = anariNewSurface(dev);
  anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &geom);
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
  float fovy = 0.5f;
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
  const uint32_t *pixels =
      (const uint32_t *)anariMapFrame(dev, frame, "channel.color", &w, &h, &pt);
  check(pixels != nullptr && w == 256 && h == 256, "frame mapped at 256x256");

  // Count non-background pixels (background is the delegate clear color).
  int coverage = 0;
  if (pixels) {
    for (uint32_t i = 0; i < w * h; ++i) {
      const uint8_t *p = (const uint8_t *)(pixels + i);
      if (p[0] > 40 || p[1] > 40 || p[2] > 40)
        ++coverage;
    }
    // Dump a PPM for eyeballing.
    std::ofstream f("render_triangle.ppm", std::ios::binary);
    f << "P6\n" << w << " " << h << "\n255\n";
    for (uint32_t i = 0; i < w * h; ++i) {
      const uint8_t *p = (const uint8_t *)(pixels + i);
      f.write((const char *)p, 3);
    }
  }
  std::printf("coverage: %d / %d px\n", coverage, w * h);
  check(coverage > 1000, "rendered triangle covers a plausible pixel area");

  const int coverage1 = coverage;
  anariUnmapFrame(dev, frame, "channel.color");

  // --- incremental zero-copy path: mutate the shared array in place ---------
  // Shrink the triangle by half through anariMapArray/Unmap. Unmap marks the
  // array's observers, so the device's next render prologue emits a
  // locator-precise DirtyPrims(points) with no topology resync.
  {
    float *mapped = (float *)anariMapArray(dev, posArray);
    for (int i = 0; i < 9; ++i)
      mapped[i] *= 0.5f;
    anariUnmapArray(dev, posArray);
  }
  anariRenderFrame(dev, frame);
  anariFrameReady(dev, frame, ANARI_WAIT);

  const uint32_t *pixels2 =
      (const uint32_t *)anariMapFrame(dev, frame, "channel.color", &w, &h, &pt);
  int coverage2 = 0;
  if (pixels2) {
    for (uint32_t i = 0; i < w * h; ++i) {
      const uint8_t *p = (const uint8_t *)(pixels2 + i);
      if (p[0] > 40 || p[1] > 40 || p[2] > 40)
        ++coverage2;
    }
  }
  std::printf(
      "coverage after in-place shrink: %d (was %d)\n", coverage2, coverage1);
  check(coverage2 > 100 && coverage2 < coverage1 * 3 / 4,
      "in-place vertex mutation reached the delegate (smaller triangle)");
  anariUnmapFrame(dev, frame, "channel.color");

  // --- lifetime stress: render then release the frame with no wait ----------
  // Exercises the self-anchoring fix: a render job is queued on the worker,
  // then the frame is released before it runs. The job must keep the frame
  // alive; a use-after-free here is caught under AddressSanitizer.
  for (int i = 0; i < 8; ++i) {
    ANARIFrame f = anariNewFrame(dev);
    anariSetParameter(dev, f, "size", ANARI_UINT32_VEC2, size);
    anariSetParameter(dev, f, "channel.color", ANARI_DATA_TYPE, &colorFmt);
    anariSetParameter(dev, f, "world", ANARI_WORLD, &world);
    anariSetParameter(dev, f, "camera", ANARI_CAMERA, &cam);
    anariSetParameter(dev, f, "renderer", ANARI_RENDERER, &ren);
    anariCommitParameters(dev, f);
    anariRenderFrame(dev, f);
    anariRelease(dev, f); // no frameReady/map: job still queued
  }
  check(
      true, "render-then-immediate-release did not crash (self-anchor holds)");

  anariRelease(dev, posArray);
  anariRelease(dev, geom);
  anariRelease(dev, mat);
  anariRelease(dev, surf);
  anariRelease(dev, surfArray);
  anariRelease(dev, world);
  anariRelease(dev, cam);
  anariRelease(dev, ren);
  anariRelease(dev, frame);
  anariRelease(dev, dev);
  anariUnloadLibrary(lib);

  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
      g_errors);
  return g_errors == 0 ? 0 : 1;
}
