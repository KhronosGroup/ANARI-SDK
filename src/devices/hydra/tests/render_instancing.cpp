// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises HdInstancer sharing: one group (a single small triangle) is
// instanced N times with distinct translations via ANARI instances. The device
// authors one prototype rprim + one instancer with N transforms (not N baked
// rprim copies). The test renders 1 vs 4 instances through Storm and asserts
// the covered area scales with the instance count — proving each instance
// transform reached the delegate.

#include <anari/anari.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
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

// Render `count` instances of a small triangle spread horizontally; return the
// covered-pixel count.
static int renderInstances(ANARIDevice dev, int count)
{
  // Small triangle prototype near the origin.
  float verts[9] = {-0.12f, -0.12f, 0.f, 0.12f, -0.12f, 0.f, 0.f, 0.14f, 0.f};
  ANARIArray1D pos =
      anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 3);
  ANARIGeometry geom = anariNewGeometry(dev, "triangle");
  anariSetParameter(dev, geom, "vertex.position", ANARI_ARRAY1D, &pos);
  anariCommitParameters(dev, geom);

  ANARIMaterial mat = anariNewMaterial(dev, "matte");
  float color[3] = {0.8f, 0.7f, 0.2f};
  anariSetParameter(dev, mat, "color", ANARI_FLOAT32_VEC3, color);
  anariCommitParameters(dev, mat);

  ANARISurface surf = anariNewSurface(dev);
  anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &geom);
  anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
  anariCommitParameters(dev, surf);

  ANARIGroup group = anariNewGroup(dev);
  ANARIArray1D surfArray =
      anariNewArray1D(dev, &surf, nullptr, nullptr, ANARI_SURFACE, 1);
  anariSetParameter(dev, group, "surface", ANARI_ARRAY1D, &surfArray);
  anariCommitParameters(dev, group);

  // N instances translated across x; column-major 4x4 (translation in col 3).
  std::vector<ANARIInstance> instances;
  for (int i = 0; i < count; ++i) {
    const float tx =
        -0.6f + 1.2f * (count == 1 ? 0.5f : float(i) / (count - 1));
    float xfm[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, tx, 0, 0, 1};
    ANARIInstance inst = anariNewInstance(dev, "transform");
    anariSetParameter(dev, inst, "group", ANARI_GROUP, &group);
    anariSetParameter(dev, inst, "transform", ANARI_FLOAT32_MAT4, xfm);
    anariCommitParameters(dev, inst);
    instances.push_back(inst);
  }

  ANARIWorld world = anariNewWorld(dev);
  ANARIArray1D instArray = anariNewArray1D(dev,
      instances.data(),
      nullptr,
      nullptr,
      ANARI_INSTANCE,
      instances.size());
  anariSetParameter(dev, world, "instance", ANARI_ARRAY1D, &instArray);
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

  anariRenderFrame(dev, frame);
  anariFrameReady(dev, frame, ANARI_WAIT);

  uint32_t w = 0, h = 0;
  ANARIDataType pt = ANARI_UNKNOWN;
  const uint32_t *pixels =
      (const uint32_t *)anariMapFrame(dev, frame, "channel.color", &w, &h, &pt);
  int coverage = 0;
  if (pixels) {
    for (uint32_t i = 0; i < w * h; ++i) {
      const uint8_t *p = (const uint8_t *)(pixels + i);
      if (p[0] > 40 || p[1] > 40 || p[2] > 40)
        ++coverage;
    }
  }
  anariUnmapFrame(dev, frame, "channel.color");

  anariRelease(dev, frame);
  anariRelease(dev, ren);
  anariRelease(dev, cam);
  anariRelease(dev, instArray);
  anariRelease(dev, world);
  for (ANARIInstance inst : instances)
    anariRelease(dev, inst);
  anariRelease(dev, surfArray);
  anariRelease(dev, group);
  anariRelease(dev, surf);
  anariRelease(dev, mat);
  anariRelease(dev, geom);
  anariRelease(dev, pos);
  return coverage;
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

  const int cov1 = renderInstances(dev, 1);
  const int cov4 = renderInstances(dev, 4);
  std::printf("coverage: 1 instance=%d, 4 instances=%d\n", cov1, cov4);

  check(cov1 > 100, "single instance renders");
  check(cov4 > cov1 * 5 / 2,
      "4 instances cover ~4x a single instance (HdInstancer transforms applied)");

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
      g_errors);
  return g_errors == 0 ? 0 : 1;
}
