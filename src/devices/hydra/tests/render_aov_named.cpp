// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises arbitrary named Hydra AOVs (SPEC §11.4, ANARI_HYDRA_FRAME_AOV): a
// frame channel parameter `channel.aov:<hydraToken>` binds that Hydra AOV and
// delivers it through the standard anariMapFrame path, the requested format
// honored via SetRenderOutputSettings. This proves the ARBITRARY path (not one
// of the standard KHR_FRAME_CHANNEL_* channels) by requesting `cameraDepth` —
// HdEmbree's GetDefaultAovDescriptor supports it (view-space linear depth,
// HdFormatFloat32) and it is NOT exposed as a standard ANARI channel.
//
// Two quads sit at clearly different depths (near z=0, far z=-2); the frame
// requests channel.color + channel.aov:cameraDepth. Checks are relative:
//   - color still renders (coverage > 0) — the color path is not broken.
//   - cameraDepth over the near quad is SMALLER than over the far quad
//     (monotonic with distance, regardless of the exact encoding).
//
// Runs on embree (which populates cameraDepth cleanly). Embree is progressive
// and renders on a background thread, so the frame's render loop polls
// convergence with a wall-clock budget (see Frame::renderOnWorker).

#include <anari/anari.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

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

// Author one axis-aligned quad at constant z and return the surface.
static ANARISurface makeQuad(
    ANARIDevice dev, float z, const float rgb[3], float half)
{
  float verts[12] = {-half, -half, z, half, -half, z, half, half, z, -half,
      half, z};
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

int main(int argc, char **argv)
{
  const char *subtype = argc > 1 ? argv[1] : "embree";

  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  if (!lib)
    return 2;
  ANARIDevice dev = anariNewDevice(lib, subtype);
  if (!dev)
    return 2;

  // ANARI_HYDRA_FRAME_AOV must be advertised for this delegate (SPEC §11.4).
  {
    const char *const *ext = nullptr;
    anariGetProperty(dev, dev, "extension", ANARI_STRING_LIST, &ext,
        sizeof(ext), ANARI_WAIT);
    bool has = false;
    for (const char *const *p = ext; p && *p; ++p)
      if (std::strcmp(*p, "ANARI_HYDRA_FRAME_AOV") == 0)
        has = true;
    check(has, "device advertises ANARI_HYDRA_FRAME_AOV");
  }

  // Near quad (small, centered) in front; far quad (large) behind.
  const float nearRGB[3] = {0.9f, 0.1f, 0.1f};
  const float farRGB[3] = {0.1f, 0.1f, 0.9f};
  ANARISurface nearSurf = makeQuad(dev, 0.0f, nearRGB, 0.4f);
  ANARISurface farSurf = makeQuad(dev, -2.0f, farRGB, 1.5f);

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
  anariSetParameter(dev, frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(dev, frame, "channel.color", ANARI_DATA_TYPE, &colorFmt);
  // The arbitrary AOV: bind Hydra's `cameraDepth` under the requested FLOAT32
  // format (SPEC §11.4). This is NOT a standard ANARI channel — it proves the
  // arbitrary `channel.aov:` path end to end.
  anariSetParameter(dev, frame, "channel.aov:cameraDepth", ANARI_DATA_TYPE, &f32);
  anariSetParameter(dev, frame, "world", ANARI_WORLD, &world);
  anariSetParameter(dev, frame, "camera", ANARI_CAMERA, &cam);
  anariSetParameter(dev, frame, "renderer", ANARI_RENDERER, &ren);
  anariCommitParameters(dev, frame);

  anariRenderFrame(dev, frame);
  anariFrameReady(dev, frame, ANARI_WAIT);

  const uint32_t W = size[0], H = size[1];
  const int cx = W / 2, cy = H / 2;
  const size_t iCenter = size_t(cy) * W + cx;

  // --- color: coverage > 0 (the color path is intact) -----------------------
  long covered = 0;
  {
    uint32_t w = 0, h = 0;
    ANARIDataType pt = ANARI_UNKNOWN;
    const uint8_t *px = (const uint8_t *)anariMapFrame(
        dev, frame, "channel.color", &w, &h, &pt);
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

  // --- arbitrary AOV cameraDepth: near < far (monotonic with distance) -------
  {
    uint32_t w = 0, h = 0;
    ANARIDataType pt = ANARI_UNKNOWN;
    const float *d = (const float *)anariMapFrame(
        dev, frame, "channel.aov:cameraDepth", &w, &h, &pt);
    check(pt == ANARI_FLOAT32, "cameraDepth pixelType is the requested FLOAT32");
    check(w == W && h == H && d != nullptr, "cameraDepth channel mapped");
    if (d) {
      // The near quad's small extent sits at the image center. Find a far-quad
      // sample: a pixel whose cameraDepth is clearly greater than the near
      // one's (robust to the exact framing of the surrounding ring).
      const float dn = d[iCenter];
      float df = dn;
      for (size_t i = 0; i < size_t(w) * h; ++i)
        if (d[i] > df)
          df = d[i];
      std::printf("  cameraDepth near(center)=%.5f  far(max)=%.5f\n", dn, df);
      check(dn > 0.f, "near-quad cameraDepth is populated (> 0)");
      check(dn < df, "near-quad cameraDepth < far-quad cameraDepth");
    }
    anariUnmapFrame(dev, frame, "channel.aov:cameraDepth");
  }

  // --- an unrequested arbitrary AOV maps to nothing (honest availability) ----
  {
    uint32_t w = 1, h = 1;
    ANARIDataType pt = ANARI_FLOAT32;
    const void *p = anariMapFrame(
        dev, frame, "channel.aov:primId", &w, &h, &pt);
    check(p == nullptr && w == 0 && h == 0 && pt == ANARI_UNKNOWN,
        "unrequested channel.aov:primId maps to nothing");
    anariUnmapFrame(dev, frame, "channel.aov:primId");
  }

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
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", g_errors);
  return g_errors == 0 ? 0 : 1;
}
