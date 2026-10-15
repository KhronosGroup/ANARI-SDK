// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Frame `duration` property: after a frame completes, an ANARI app queries
// anariGetProperty(dev, frame, "duration", ANARI_FLOAT32, ...) to display frame
// latency (e.g. anari_viewer's Viewport HUD). This drives a minimal
// single-triangle scene, renders, and asserts the reported duration is a sane
// positive wall-clock. The timing path is delegate-independent, so this runs on
// BOTH storm (one-Execute) and embree (progressive converge loop).

#include <anari/anari.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

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

  ANARIDevice dev = anariNewDevice(lib, subtype);
  check(dev != nullptr, "device created for delegate subtype");
  if (!dev)
    return 2;

  // --- minimal scene: one triangle (cloned from render_triangle.cpp) --------
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

  // --- the property under test ----------------------------------------------
  float d = -1.f;
  bool ok = anariGetProperty(
      dev, frame, "duration", ANARI_FLOAT32, &d, sizeof(d), ANARI_WAIT);
  std::printf("duration: %g s\n", d);
  check(ok, "getProperty(duration, ANARI_FLOAT32) returns true");
  check(d > 0.f, "reported duration is positive");
  check(d < 30.f, "reported duration is below the 30s converge deadline");

  // --- negative checks: bad name / bad type do not report or write ----------
  {
    float bad = -1.f;
    bool nOk = anariGetProperty(dev,
        frame,
        "nonexistent",
        ANARI_FLOAT32,
        &bad,
        sizeof(bad),
        ANARI_WAIT);
    check(!nOk && bad == -1.f,
        "unknown property name returns false and leaves ptr untouched");
  }
  {
    double bad = -1.0;
    bool tOk = anariGetProperty(
        dev, frame, "duration", ANARI_FLOAT64, &bad, sizeof(bad), ANARI_WAIT);
    check(!tOk && bad == -1.0,
        "duration with wrong type returns false and leaves ptr untouched");
  }

  // --- re-measured, not one-shot: render again and re-query -----------------
  anariRenderFrame(dev, frame);
  anariFrameReady(dev, frame, ANARI_WAIT);
  float d2 = -1.f;
  bool ok2 = anariGetProperty(
      dev, frame, "duration", ANARI_FLOAT32, &d2, sizeof(d2), ANARI_WAIT);
  std::printf("duration (2nd render): %g s\n", d2);
  check(ok2 && d2 > 0.f && d2 < 30.f,
      "second render re-measures a fresh positive duration");

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
