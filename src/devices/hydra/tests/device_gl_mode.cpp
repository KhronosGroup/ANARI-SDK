// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// The device's GL context selection (ANARI_HYDRA_GL) and its clean-failure
// path, through the public API. All in one process, in this order:
//   - none + storm: anariNewDevice returns null and reports an error (Storm
//     needs GL); it must not crash.
//   - none + embree: the device works, renders a triangle, and reports
//     "GL context none".
//   - an invalid ANARI_HYDRA_GL, and a forced egl on a device index that does
//     not exist: null device plus an error naming the cause.
//   - auto + storm: a device on EGL or GLX that renders. Running it after the
//     failed none + storm attempt also checks that the failure left no state
//     behind (Garch loads its GL entry points once per process). Skipped, not
//     failed, when the machine has no GL at all.

#include <anari/anari.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int g_failures = 0;

struct Message
{
  ANARIStatusSeverity severity;
  ANARIStatusCode code;
  std::string text;
};
static std::vector<Message> g_messages;

static void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message)
{
  g_messages.push_back({severity, code, message ? message : ""});
  if (severity <= ANARI_SEVERITY_WARNING)
    std::printf("    [%s] %s\n",
        severity <= ANARI_SEVERITY_ERROR ? "ERROR" : "WARN",
        message);
  else if (message && std::strstr(message, "GL context"))
    std::printf("    [INFO] %s\n", message);
}

static void check(bool ok, const char *what)
{
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok)
    ++g_failures;
}

static const Message *findMessage(
    ANARIStatusSeverity severity, const char *text)
{
  for (const Message &m : g_messages)
    if (m.severity == severity && m.text.find(text) != std::string::npos)
      return &m;
  return nullptr;
}

static int countErrors()
{
  int n = 0;
  for (const Message &m : g_messages)
    n += m.severity <= ANARI_SEVERITY_ERROR;
  return n;
}

static void setMode(const char *mode)
{
  if (mode)
    setenv("ANARI_HYDRA_GL", mode, 1);
  else
    unsetenv("ANARI_HYDRA_GL");
  g_messages.clear();
}

// Render one triangle; return the number of non-background pixels.
static int renderTriangle(ANARIDevice dev)
{
  float verts[9] = {-0.8f, -0.8f, 0.f, 0.8f, -0.8f, 0.f, 0.f, 0.9f, 0.f};
  ANARIArray1D pos =
      anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 3);
  ANARIGeometry geom = anariNewGeometry(dev, "triangle");
  anariSetParameter(dev, geom, "vertex.position", ANARI_ARRAY1D, &pos);
  anariCommitParameters(dev, geom);
  ANARIMaterial mat = anariNewMaterial(dev, "matte");
  float color[3] = {0.9f, 0.5f, 0.1f};
  anariSetParameter(dev, mat, "color", ANARI_FLOAT32_VEC3, color);
  anariCommitParameters(dev, mat);
  ANARISurface surf = anariNewSurface(dev);
  anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &geom);
  anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
  anariCommitParameters(dev, surf);
  ANARIArray1D surfs =
      anariNewArray1D(dev, &surf, nullptr, nullptr, ANARI_SURFACE, 1);
  ANARIWorld world = anariNewWorld(dev);
  anariSetParameter(dev, world, "surface", ANARI_ARRAY1D, &surfs);
  anariCommitParameters(dev, world);

  ANARICamera cam = anariNewCamera(dev, "perspective");
  float eye[3] = {0.f, 0.f, 3.f}, dir[3] = {0.f, 0.f, -1.f},
        up[3] = {0.f, 1.f, 0.f};
  anariSetParameter(dev, cam, "position", ANARI_FLOAT32_VEC3, eye);
  anariSetParameter(dev, cam, "direction", ANARI_FLOAT32_VEC3, dir);
  anariSetParameter(dev, cam, "up", ANARI_FLOAT32_VEC3, up);
  anariCommitParameters(dev, cam);
  ANARIRenderer ren = anariNewRenderer(dev, "default");
  anariCommitParameters(dev, ren);

  ANARIFrame frame = anariNewFrame(dev);
  uint32_t size[2] = {64, 64};
  ANARIDataType fmt = ANARI_UFIXED8_RGBA_SRGB;
  anariSetParameter(dev, frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(dev, frame, "channel.color", ANARI_DATA_TYPE, &fmt);
  anariSetParameter(dev, frame, "world", ANARI_WORLD, &world);
  anariSetParameter(dev, frame, "camera", ANARI_CAMERA, &cam);
  anariSetParameter(dev, frame, "renderer", ANARI_RENDERER, &ren);
  anariCommitParameters(dev, frame);
  anariRenderFrame(dev, frame);
  anariFrameReady(dev, frame, ANARI_WAIT);

  uint32_t w = 0, h = 0;
  ANARIDataType type = ANARI_UNKNOWN;
  auto *px = (const uint8_t *)anariMapFrame(
      dev, frame, "channel.color", &w, &h, &type);
  int coverage = 0;
  for (uint32_t i = 0; px && i < w * h; ++i)
    coverage += px[4 * i] > 40 || px[4 * i + 1] > 40 || px[4 * i + 2] > 40;
  anariUnmapFrame(dev, frame, "channel.color");

  for (ANARIObject o : {(ANARIObject)frame,
           (ANARIObject)ren,
           (ANARIObject)cam,
           (ANARIObject)world,
           (ANARIObject)surfs,
           (ANARIObject)surf,
           (ANARIObject)mat,
           (ANARIObject)geom,
           (ANARIObject)pos})
    anariRelease(dev, o);
  return coverage;
}

// Expect anariNewDevice to fail cleanly: null, plus an error naming `cause`.
static void expectRefused(
    ANARILibrary lib, const char *subtype, const char *cause, const char *what)
{
  ANARIDevice dev = anariNewDevice(lib, subtype);
  check(dev == nullptr, what);
  if (dev)
    anariRelease(dev, dev);
  const Message *err = findMessage(ANARI_SEVERITY_ERROR, cause);
  std::printf("    (expecting an error mentioning \"%s\")\n", cause);
  check(err != nullptr, "  ... with an error status message naming the cause");
  check(err && err->code == ANARI_STATUS_UNSUPPORTED_DEVICE,
      "  ... with status code ANARI_STATUS_UNSUPPORTED_DEVICE");
}

int main()
{
  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  if (!lib) {
    std::printf("FAIL: could not load anari library 'hydra'\n");
    return 2;
  }

  std::printf("ANARI_HYDRA_GL=none, storm:\n");
  setMode("none");
  expectRefused(lib,
      "storm",
      "storm",
      "storm is refused without a GL context (null device)");

  std::printf("ANARI_HYDRA_GL=none, embree:\n");
  setMode("none");
  ANARIDevice dev = anariNewDevice(lib, "embree");
  check(dev != nullptr, "embree device created without a GL context");
  if (dev) {
    check(findMessage(ANARI_SEVERITY_INFO, "GL context none") != nullptr,
        "the device reports \"GL context none\"");
    const int coverage = renderTriangle(dev);
    std::printf("    coverage %d / 4096\n", coverage);
    check(coverage > 300, "embree renders a triangle without GL");
    check(countErrors() == 0, "no error messages");
    anariRelease(dev, dev);
  }

  std::printf("ANARI_HYDRA_GL=bogus, embree:\n");
  setMode("bogus");
  expectRefused(lib,
      "embree",
      "ANARI_HYDRA_GL",
      "an invalid ANARI_HYDRA_GL is refused (null device)");

  std::printf("ANARI_HYDRA_GL=egl, ANARI_HYDRA_EGL_DEVICE=999, embree:\n");
  setMode("egl");
  setenv("ANARI_HYDRA_EGL_DEVICE", "999", 1);
  expectRefused(lib,
      "embree",
      "EGL",
      "a forced, unavailable EGL context is refused (null device)");
  unsetenv("ANARI_HYDRA_EGL_DEVICE");

  std::printf("ANARI_HYDRA_GL unset (auto), storm:\n");
  setMode(nullptr);
  dev = anariNewDevice(lib, "storm");
  if (!dev && findMessage(ANARI_SEVERITY_ERROR, "GL context none")) {
    std::printf("  [SKIP] no GL context available on this machine\n");
  } else {
    check(dev != nullptr, "storm device created in auto mode");
    const bool named = findMessage(ANARI_SEVERITY_INFO, "GL context EGL (")
        || findMessage(ANARI_SEVERITY_INFO, "GL context GLX (");
    check(named, "the device reports its EGL or GLX context");
    if (dev) {
      const int coverage = renderTriangle(dev);
      std::printf("    coverage %d / 4096\n", coverage);
      check(coverage > 300, "storm renders a triangle");
      check(countErrors() == 0, "no error messages");
      anariRelease(dev, dev);
    }
  }

  anariUnloadLibrary(lib);
  std::printf(g_failures ? "\nCHECKS FAILED (%d issues)\n"
                         : "\nALL CHECKS PASSED (%d issues)\n",
      g_failures);
  return g_failures ? 1 : 0;
}
