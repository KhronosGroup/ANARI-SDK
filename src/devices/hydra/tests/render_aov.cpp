// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises the standard AOV frame channels beyond color. Two quads sit at
// clearly different depths (near z=0, far z=-2); the frame requests
// channel.color + channel.depth + channel.objectId + channel.normal, and the
// checks include numerical distances and native depth passthrough:
//   - color still renders (coverage > 0) — the color path is not broken.
//   - depth is Euclidean camera distance, including off-axis and resized
//   pixels.
//   - misses are infinity; native channel.aov:depth retains raster semantics.
//   - objectId over the two surfaces differs, and both differ from background.
//   - normal over a +z-facing quad has a dominant |z| component (world-space
//     directions, rotations, precision: render_normal_channel).
//
// Runs on both storm and embree. Embree is a progressive delegate that renders
// on a background thread, so the frame's render loop polls convergence with a
// wall-clock budget (see Frame::renderOnWorker) — a fixed spin count could read
// back before the async render finishes.

#include <anari/anari.h>

#include <cmath>
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

// Author one axis-aligned quad at constant z, given its two attribute0 corners
// (unused here but keeps the geometry mesh-simple), and return the surface.
static ANARISurface makeQuad(
    ANARIDevice dev, float z, const float rgb[3], float half)
{
  float verts[12] = {
      -half, -half, z, half, -half, z, half, half, z, -half, half, z};
  ANARIArray1D pos =
      anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  ANARIGeometry g = anariNewGeometry(dev, "quad");
  anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
  anariCommitParameters(dev, g);

  ANARIMaterial mat = anariNewMaterial(dev, "matte");
  anariSetParameter(dev, mat, "color", ANARI_FLOAT32_VEC3, rgb);
  anariCommitParameters(dev, mat);

  ANARISurface surf = anariNewSurface(dev);
  anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &g);
  anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
  anariCommitParameters(dev, surf);

  anariRelease(dev, pos);
  anariRelease(dev, g);
  anariRelease(dev, mat);
  return surf;
}

// Expected distances come from plane intersections with perspective rays.
static void checkDepth(ANARIDevice dev,
    ANARIFrame frame,
    uint32_t width,
    uint32_t height,
    float fovy)
{
  uint32_t w = 0, h = 0;
  ANARIDataType type = ANARI_UNKNOWN;
  const float *depth = static_cast<const float *>(
      anariMapFrame(dev, frame, "channel.depth", &w, &h, &type));
  check(depth && w == width && h == height && type == ANARI_FLOAT32,
      "depth has the current frame dimensions and type");
  const float *native = static_cast<const float *>(
      anariMapFrame(dev, frame, "channel.aov:depth", &w, &h, &type));
  check(native && w == width && h == height && type == ANARI_FLOAT32,
      "native depth maps alongside standard depth");
  if (depth && native) {
    for (int sample = 0; sample < 3; ++sample) {
      const uint32_t x = sample == 0
          ? width / 2
          : uint32_t(width * (sample == 1 ? 0.53f : 0.65f));
      const uint32_t y = height / 2;
      const double sx = (2.0 * (x + 0.5) / width - 1.0)
          * (double(width) / height) * std::tan(fovy / 2.0);
      const double sy = (2.0 * (y + 0.5) / height - 1.0) * std::tan(fovy / 2.0);
      const double expected =
          (sample == 2 ? 5.0 : 3.0) * std::sqrt(1.0 + sx * sx + sy * sy);
      const size_t i = size_t(y) * width + x;
      std::printf("  depth at (%u,%u): %.6f expected %.6f native %.6f\n",
          x,
          y,
          depth[i],
          expected,
          native[i]);
      check(std::fabs(depth[i] - expected) < 0.01,
          "depth is Euclidean distance to the camera");
      check(native[i] > 0.f && native[i] < 1.f,
          "arbitrary native depth retains normalized raster semantics");
    }
    check(std::isinf(depth[0]) && depth[0] > 0.f,
        "miss depth is positive infinity");
    check(native[0] == 1.f, "native miss depth retains its clear value");
  }
  anariUnmapFrame(dev, frame, "channel.aov:depth");
  anariUnmapFrame(dev, frame, "channel.depth");
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

  // Near quad (small, centered) in front; far quad (large) behind. The near one
  // covers the image center; the far one shows around its edges.
  const float nearRGB[3] = {0.9f, 0.1f, 0.1f};
  const float farRGB[3] = {0.1f, 0.1f, 0.9f};
  ANARISurface nearSurf = makeQuad(dev, 0.0f, nearRGB, 0.4f);
  ANARISurface farSurf = makeQuad(dev, -2.0f, farRGB, 1.5f);

  uint32_t nearId = 0xf1234567u, farId = 0x23456789u;
  anariSetParameter(dev, nearSurf, "id", ANARI_UINT32, &nearId);
  anariSetParameter(dev, farSurf, "id", ANARI_UINT32, &farId);
  anariCommitParameters(dev, nearSurf);
  anariCommitParameters(dev, farSurf);
  ANARISurface surfs[2] = {nearSurf, farSurf};
  ANARIArray1D surfArray =
      anariNewArray1D(dev, surfs, nullptr, nullptr, ANARI_SURFACE, 2);
  ANARIWorld world = anariNewWorld(dev);
  anariSetParameter(dev, world, "surface", ANARI_ARRAY1D, &surfArray);
  anariCommitParameters(dev, world);

  ANARICamera cam = anariNewCamera(dev, "perspective");
  float eye[3] = {0.f, 0.f, 3.f};
  float dir[3] = {0.f, 0.f, -1.f};
  float up[3] = {0.f, 1.f, 0.f};
  float fovy = 0.9f;
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
  ANARIDataType f32 = ANARI_FLOAT32;
  ANARIDataType f32v3 = ANARI_FLOAT32_VEC3;
  ANARIDataType u32 = ANARI_UINT32;
  anariSetParameter(dev, frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(dev, frame, "channel.color", ANARI_DATA_TYPE, &colorFmt);
  anariSetParameter(dev, frame, "channel.depth", ANARI_DATA_TYPE, &f32);
  anariSetParameter(dev, frame, "channel.aov:depth", ANARI_DATA_TYPE, &f32);
  anariSetParameter(dev, frame, "channel.normal", ANARI_DATA_TYPE, &f32v3);
  anariSetParameter(dev, frame, "channel.objectId", ANARI_DATA_TYPE, &u32);
  anariSetParameter(dev, frame, "world", ANARI_WORLD, &world);
  anariSetParameter(dev, frame, "camera", ANARI_CAMERA, &cam);
  anariSetParameter(dev, frame, "renderer", ANARI_RENDERER, &ren);
  anariCommitParameters(dev, frame);

  anariRenderFrame(dev, frame);
  anariFrameReady(dev, frame, ANARI_WAIT);

  const uint32_t W = size[0], H = size[1];

  // --- color: coverage > 0 (the color path is intact) -----------------------
  {
    uint32_t w = 0, h = 0;
    ANARIDataType pt = ANARI_UNKNOWN;
    const uint8_t *px = (const uint8_t *)anariMapFrame(
        dev, frame, "channel.color", &w, &h, &pt);
    long covered = 0;
    if (px)
      for (size_t i = 0; i < size_t(w) * h; ++i) {
        const uint8_t *p = px + i * 4;
        if (p[0] > 20 || p[1] > 20 || p[2] > 20)
          ++covered;
      }
    std::printf("  color %ux%u covered=%ld\n", w, h, covered);
    check(w == W && h == H, "color channel is frame-sized");
    check(covered > 0, "color renders (coverage > 0)");
    anariUnmapFrame(dev, frame, "channel.color");
  }

  // The near quad's small extent sits at the image center; the center pixel is
  // therefore the near quad. The far quad shows in a ring around it — find one
  // of its pixels from the objectId buffer (its id differs from the near quad's
  // and from the background), so the far sample is robust to the exact framing.
  const int cx = W / 2, cy = H / 2;
  const size_t iCenter = size_t(cy) * W + cx;
  const uint32_t bg = 0xFFFFFFFFu; // Hydra background primId is -1.

  // --- objectId: near != far, both != background ----------------------------
  size_t iFar = iCenter;
  uint32_t idN = bg, idF = bg;
  {
    uint32_t w = 0, h = 0;
    ANARIDataType pt = ANARI_UNKNOWN;
    const uint32_t *id = (const uint32_t *)anariMapFrame(
        dev, frame, "channel.objectId", &w, &h, &pt);
    check(pt == ANARI_UINT32, "objectId pixelType is UINT32");
    check(w == W && h == H && id != nullptr, "objectId channel mapped");
    if (id) {
      idN = id[iCenter];
      for (size_t i = 0; i < size_t(w) * h; ++i)
        if (id[i] != idN && id[i] != bg) {
          iFar = i;
          idF = id[i];
          break;
        }
      std::printf(
          "  objectId near=0x%08x  far=0x%08x  (bg=0x%08x)\n", idN, idF, bg);
      check(idN == nearId,
          "near-quad objectId is a real surface (not background)");
      check(iFar != iCenter, "a distinct far-quad surface pixel exists");
      check(idN != idF, "near-quad objectId != far-quad objectId");
      check(idF == farId, "far-quad objectId differs from background");
    }
    anariUnmapFrame(dev, frame, "channel.objectId");
  }

  // --- depth: near < far (monotonic with distance) --------------------------
  {
    uint32_t w = 0, h = 0;
    ANARIDataType pt = ANARI_UNKNOWN;
    const float *d =
        (const float *)anariMapFrame(dev, frame, "channel.depth", &w, &h, &pt);
    check(pt == ANARI_FLOAT32, "depth pixelType is FLOAT32");
    check(w == W && h == H && d != nullptr, "depth channel mapped");
    if (d) {
      const float dn = d[iCenter];
      const float df = d[iFar];
      std::printf("  depth near(center)=%.5f  far=%.5f\n", dn, df);
      check(dn < df, "near-quad depth < far-quad depth");
    }
    anariUnmapFrame(dev, frame, "channel.depth");
  }

  // --- normal: +z-facing quad has a dominant |z| component ------------------
  // Both delegates serve channel.normal (embree from its `normal` AOV, storm
  // from `Neye` rotated to world space); the quads carry no authored normals,
  // so this is the delegate's own geometric normal.
  {
    uint32_t w = 0, h = 0;
    ANARIDataType pt = ANARI_UNKNOWN;
    const float *nrm =
        (const float *)anariMapFrame(dev, frame, "channel.normal", &w, &h, &pt);
    check(pt == ANARI_FLOAT32_VEC3, "normal pixelType is FLOAT32_VEC3");
    const bool mapped = nrm && w == W && h == H;
    check(mapped, "normal channel mapped at frame size");
    if (mapped) {
      const float *n = nrm + iCenter * 3;
      const float ax = std::fabs(n[0]), ay = std::fabs(n[1]),
                  az = std::fabs(n[2]);
      std::printf("  normal near-quad = %.3f %.3f %.3f\n", n[0], n[1], n[2]);
      check((ax + ay + az) > 1e-4f, "near-quad normal is populated");
      check(az >= ax && az >= ay, "near-quad normal has dominant |z|");
    }
    anariUnmapFrame(dev, frame, "channel.normal");
  }

  checkDepth(dev, frame, size[0], size[1], fovy);
  size[0] = 320;
  size[1] = 160;
  anariSetParameter(dev, frame, "size", ANARI_UINT32_VEC2, size);
  anariCommitParameters(dev, frame);
  anariRenderFrame(dev, frame);
  anariFrameReady(dev, frame, ANARI_WAIT);
  checkDepth(dev, frame, size[0], size[1], fovy);

  anariRelease(dev, frame);
  anariRelease(dev, ren);
  anariRelease(dev, cam);
  anariRelease(dev, world);
  anariRelease(dev, surfArray);
  anariRelease(dev, nearSurf);
  anariRelease(dev, farSurf);

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
      g_errors);
  return g_errors == 0 ? 0 : 1;
}
