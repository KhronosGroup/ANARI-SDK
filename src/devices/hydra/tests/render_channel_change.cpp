// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Regression test for an app's pick pattern (Vela): a display frame starts
// with only channel.color, later gains channel.objectId (a selection outline),
// and every pick renders and releases a temporary frame carrying depth and ID
// channels. The renderer's ambient fill is on, so each frame also owns a dome
// light.
//
// A display frame with more than one render output once left Storm's viewport
// AOV empty: HdxAovInputTask then stopped refreshing the task context's
// texture handles while HdxVisualizeAovTask kept drawing with them, binding a
// deleted texture ("Attachment not a texture") and later crashing in HgiGL's
// framebuffer-cache garbage collection. TF_FATAL_VERIFY turns that failed
// verification into an immediate abort, so the test fails deterministically
// instead of depending on when the freed memory is reused.

#include <anari/anari.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

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

static const uint32_t kWidth = 64, kHeight = 48;
static const uint32_t kSurfaceId = 7;

static void setChannel(
    ANARIDevice d, ANARIFrame f, const char *channel, ANARIDataType type)
{
  anariSetParameter(d, f, channel, ANARI_DATA_TYPE, &type);
}

static uint32_t centerU32(ANARIDevice d, ANARIFrame f, const char *channel)
{
  uint32_t w = 0, h = 0;
  ANARIDataType type = ANARI_UNKNOWN;
  const auto *p = static_cast<const uint32_t *>(
      anariMapFrame(d, f, channel, &w, &h, &type));
  const uint32_t v =
      p && w == kWidth && h == kHeight ? p[(h / 2) * w + w / 2] : ~0u;
  anariUnmapFrame(d, f, channel);
  return v;
}

// Render the display frame and check its color and (when enabled) objectId.
static void renderDisplay(ANARIDevice d, ANARIFrame display, bool objectId)
{
  anariRenderFrame(d, display);
  anariFrameReady(d, display, ANARI_WAIT);
  check(centerU32(d, display, "channel.color") != ~0u, "display color maps");
  if (objectId)
    check(centerU32(d, display, "channel.objectId") == kSurfaceId,
        "display objectId hits the surface");
}

// One pick: a temporary frame sharing the display's camera, renderer and world,
// with depth and ID channels and no color, released right after the readback.
static void pick(
    ANARIDevice d, ANARICamera camera, ANARIRenderer renderer, ANARIWorld world)
{
  ANARIFrame f = anariNewFrame(d);
  const uint32_t size[] = {kWidth, kHeight};
  anariSetParameter(d, f, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(d, f, "camera", ANARI_CAMERA, &camera);
  anariSetParameter(d, f, "renderer", ANARI_RENDERER, &renderer);
  anariSetParameter(d, f, "world", ANARI_WORLD, &world);
  setChannel(d, f, "channel.depth", ANARI_FLOAT32);
  setChannel(d, f, "channel.objectId", ANARI_UINT32);
  setChannel(d, f, "channel.primitiveId", ANARI_UINT32);
  setChannel(d, f, "channel.instanceId", ANARI_UINT32);
  anariCommitParameters(d, f);
  anariRenderFrame(d, f);
  anariFrameReady(d, f, ANARI_WAIT);
  check(centerU32(d, f, "channel.objectId") == kSurfaceId,
      "pick objectId hits the surface");
  check(centerU32(d, f, "channel.primitiveId") == 0,
      "pick primitiveId hits triangle 0");
  anariRelease(d, f);
}

int main(int argc, char **argv)
{
  // Must precede the first TfGetenv of the setting, i.e. loading the device.
  setenv("TF_FATAL_VERIFY", "1", 1);

  const char *delegate = argc > 1 ? argv[1] : "storm";
  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  if (!lib)
    return 2;
  ANARIDevice d = anariNewDevice(lib, delegate);
  if (!d)
    return 2;

  ANARICamera camera = anariNewCamera(d, "perspective");
  const float eye[] = {0.f, 0.f, 3.f}, dir[] = {0.f, 0.f, -1.f};
  const float aspect = float(kWidth) / float(kHeight);
  anariSetParameter(d, camera, "position", ANARI_FLOAT32_VEC3, eye);
  anariSetParameter(d, camera, "direction", ANARI_FLOAT32_VEC3, dir);
  anariSetParameter(d, camera, "aspect", ANARI_FLOAT32, &aspect);
  anariCommitParameters(d, camera);

  ANARIRenderer renderer = anariNewRenderer(d, "default");
  const float ambientRadiance = 0.25f;
  anariSetParameter(
      d, renderer, "ambientRadiance", ANARI_FLOAT32, &ambientRadiance);
  anariCommitParameters(d, renderer);

  const float vertices[] = {-1.f, -1.f, 0.f, 1.f, -1.f, 0.f, 0.f, 1.f, 0.f};
  ANARIArray1D position =
      anariNewArray1D(d, vertices, nullptr, nullptr, ANARI_FLOAT32_VEC3, 3);
  ANARIGeometry geometry = anariNewGeometry(d, "triangle");
  anariSetParameter(d, geometry, "vertex.position", ANARI_ARRAY1D, &position);
  anariCommitParameters(d, geometry);
  anariRelease(d, position);
  ANARIMaterial material = anariNewMaterial(d, "matte");
  anariCommitParameters(d, material);
  ANARISurface surface = anariNewSurface(d);
  anariSetParameter(d, surface, "geometry", ANARI_GEOMETRY, &geometry);
  anariSetParameter(d, surface, "material", ANARI_MATERIAL, &material);
  anariSetParameter(d, surface, "id", ANARI_UINT32, &kSurfaceId);
  anariCommitParameters(d, surface);
  anariRelease(d, geometry);
  anariRelease(d, material);
  ANARIArray1D surfaces =
      anariNewArray1D(d, &surface, nullptr, nullptr, ANARI_SURFACE, 1);
  ANARIWorld world = anariNewWorld(d);
  anariSetParameter(d, world, "surface", ANARI_ARRAY1D, &surfaces);
  anariCommitParameters(d, world);
  anariRelease(d, surfaces);
  anariRelease(d, surface);

  ANARIFrame display = anariNewFrame(d);
  const uint32_t size[] = {kWidth, kHeight};
  anariSetParameter(d, display, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(d, display, "camera", ANARI_CAMERA, &camera);
  anariSetParameter(d, display, "renderer", ANARI_RENDERER, &renderer);
  anariSetParameter(d, display, "world", ANARI_WORLD, &world);
  setChannel(d, display, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  anariCommitParameters(d, display);

  std::printf("Color-only display frame, then a pick\n");
  renderDisplay(d, display, false);
  pick(d, camera, renderer, world);

  std::printf("Display frame gains channel.objectId, then more picks\n");
  setChannel(d, display, "channel.objectId", ANARI_UINT32);
  anariCommitParameters(d, display);
  for (int i = 0; i < 3; ++i) {
    renderDisplay(d, display, true);
    pick(d, camera, renderer, world);
  }
  renderDisplay(d, display, true);

  anariRelease(d, display);
  anariRelease(d, world);
  anariRelease(d, renderer);
  anariRelease(d, camera);
  anariRelease(d, d);
  anariUnloadLibrary(lib);

  std::printf("%s\n", g_errors == 0 ? "ALL PASS" : "FAILURES");
  return g_errors == 0 ? 0 : 1;
}
