// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Rebuilding a mesh whose points a delegate still holds. HdEmbree keeps the
// points VtArray it pulled in the previous Sync (HdEmbreeMesh::_points), and
// that array refers to the device's zero-copy wrapper for the ANARI array.
// Re-pointing the geometry at a new position array replaces the wrapper, so
// the wrapper's lifetime must not end while the delegate's copy is alive:
// when it did, the next Sync's `_points = ...` decremented a freed refcount,
// corrupting whatever was allocated there next (a new mesh's topology), and
// the process crashed in HdSceneIndexAdapterSceneDelegate::GetMeshTopology.
//
// The scene: one quad, render, re-point it at new positions and add a second
// quad, render again. Both renders must complete and show both quads.

#include <anari/anari.h>

#include <cstdint>
#include <cstdio>

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

static ANARIGeometry newQuad(ANARIDevice dev, ANARIArray1D positions)
{
  ANARIGeometry geom = anariNewGeometry(dev, "quad");
  anariSetParameter(dev, geom, "vertex.position", ANARI_ARRAY1D, &positions);
  anariCommitParameters(dev, geom);
  return geom;
}

static ANARISurface newSurface(
    ANARIDevice dev, ANARIGeometry geom, ANARIMaterial mat)
{
  ANARISurface surf = anariNewSurface(dev);
  anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &geom);
  anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
  anariCommitParameters(dev, surf);
  return surf;
}

// Covered (non-clear) pixels in rows [y0,y1) of the centre column band.
static int coveredRows(ANARIDevice dev, ANARIFrame frame, float y0, float y1)
{
  anariRenderFrame(dev, frame);
  anariFrameReady(dev, frame, ANARI_WAIT);
  uint32_t w = 0, h = 0;
  ANARIDataType type = ANARI_UNKNOWN;
  const uint32_t *px = (const uint32_t *)anariMapFrame(
      dev, frame, "channel.color", &w, &h, &type);
  int covered = 0;
  for (uint32_t y = uint32_t(y0 * h); px && y < uint32_t(y1 * h); ++y) {
    for (uint32_t x = 2 * w / 5; x < 3 * w / 5; ++x) {
      const uint8_t *p = (const uint8_t *)(px + y * w + x);
      if (p[0] > 40 || p[1] > 40 || p[2] > 40)
        ++covered;
    }
  }
  anariUnmapFrame(dev, frame, "channel.color");
  return covered;
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

  // Upper quad: y in [0.2, 0.8]; lower quad: y in [-0.8, -0.2].
  float upper[12] = {
      -.5f, .2f, 0, .5f, .2f, 0, .5f, .8f, 0, -.5f, .8f, 0};
  float lower[12] = {
      -.5f, -.8f, 0, .5f, -.8f, 0, .5f, -.2f, 0, -.5f, -.2f, 0};
  ANARIArray1D upperPositions =
      anariNewArray1D(dev, upper, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  ANARIArray1D lowerPositions =
      anariNewArray1D(dev, lower, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);

  ANARIMaterial mat = anariNewMaterial(dev, "matte");
  const float white[3] = {1, 1, 1};
  anariSetParameter(dev, mat, "color", ANARI_FLOAT32_VEC3, white);
  anariCommitParameters(dev, mat);

  ANARIGeometry first = newQuad(dev, upperPositions);
  ANARISurface firstSurface = newSurface(dev, first, mat);
  ANARIWorld world = anariNewWorld(dev);
  ANARIArray1D oneSurface =
      anariNewArray1D(dev, &firstSurface, nullptr, nullptr, ANARI_SURFACE, 1);
  anariSetParameter(dev, world, "surface", ANARI_ARRAY1D, &oneSurface);
  anariCommitParameters(dev, world);

  ANARICamera cam = anariNewCamera(dev, "perspective");
  float eye[3] = {0.f, 0.f, 3.f};
  float dir[3] = {0.f, 0.f, -1.f};
  float up[3] = {0.f, 1.f, 0.f};
  anariSetParameter(dev, cam, "position", ANARI_FLOAT32_VEC3, eye);
  anariSetParameter(dev, cam, "direction", ANARI_FLOAT32_VEC3, dir);
  anariSetParameter(dev, cam, "up", ANARI_FLOAT32_VEC3, up);
  anariCommitParameters(dev, cam);

  ANARIRenderer ren = anariNewRenderer(dev, "default");
  anariCommitParameters(dev, ren);

  ANARIFrame frame = anariNewFrame(dev);
  uint32_t size[2] = {128, 128};
  ANARIDataType colorFmt = ANARI_UFIXED8_RGBA_SRGB;
  anariSetParameter(dev, frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(dev, frame, "channel.color", ANARI_DATA_TYPE, &colorFmt);
  anariSetParameter(dev, frame, "world", ANARI_WORLD, &world);
  anariSetParameter(dev, frame, "camera", ANARI_CAMERA, &cam);
  anariSetParameter(dev, frame, "renderer", ANARI_RENDERER, &ren);
  anariCommitParameters(dev, frame);

  // Image rows run bottom to top, so the upper quad is in the top half.
  check(coveredRows(dev, frame, .6f, .9f) > 50, "first quad renders (upper)");

  // Move the first quad down (a new position array replaces its wrapper) and
  // add a second quad at the old place, then render again.
  anariSetParameter(dev, first, "vertex.position", ANARI_ARRAY1D, &lowerPositions);
  anariCommitParameters(dev, first);
  ANARIGeometry second = newQuad(dev, upperPositions);
  ANARISurface secondSurface = newSurface(dev, second, mat);
  const ANARISurface both[2] = {firstSurface, secondSurface};
  ANARIArray1D twoSurfaces =
      anariNewArray1D(dev, both, nullptr, nullptr, ANARI_SURFACE, 2);
  anariSetParameter(dev, world, "surface", ANARI_ARRAY1D, &twoSurfaces);
  anariCommitParameters(dev, world);

  check(coveredRows(dev, frame, .1f, .4f) > 50, "moved quad renders (lower)");
  check(coveredRows(dev, frame, .6f, .9f) > 50, "added quad renders (upper)");

  anariRelease(dev, frame);
  anariRelease(dev, ren);
  anariRelease(dev, cam);
  anariRelease(dev, twoSurfaces);
  anariRelease(dev, oneSurface);
  anariRelease(dev, world);
  anariRelease(dev, secondSurface);
  anariRelease(dev, firstSurface);
  anariRelease(dev, second);
  anariRelease(dev, first);
  anariRelease(dev, mat);
  anariRelease(dev, lowerPositions);
  anariRelease(dev, upperPositions);

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", g_errors);
  return g_errors == 0 ? 0 : 1;
}
