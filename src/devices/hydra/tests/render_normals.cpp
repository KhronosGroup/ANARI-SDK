// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises app-provided vertex.normal -> the `normals` primvar. The device
// authors only application-supplied normals; it never generates, smooths, or
// falls back to computed normals. The falsifiable discriminator is shaded
// brightness under a fixed side-light while varying ONLY the authored normals:
// a front-facing quad (geometric normal +Z) lit by a light traveling toward -X
// (so a +X surface normal faces the light).
//
//   Render A: vertex.normal = (+1,0,0)  -> faces the light -> bright.
//   Render B: vertex.normal = (-1,0,0)  -> faces away       -> dark.
//
// Fails today (normals dropped): both fall back to the geometric +Z normal, for
// which N·L == 0 under the side-light, so A == B. Goes green once the authored
// normals reach Storm.
//
// storm-only: the brightness split is measured against Storm's matte +
// directional lighting (like the other render_* brightness tests). The
// authoring path itself is delegate-independent, but embree's matte +
// directional shading in this harness produces no stable split (both renders
// read equal), so the acceptance assert is storm-only (mirrors the other
// storm-only render tests).

#include <anari/anari.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

static int g_errors = 0;
static const char *g_normalParameter = "vertex.normal";

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

// Render a front-facing quad (geometric normal +Z) under a directional light
// traveling toward -X, optionally authoring a uniform vertex.normal. Returns
// {mean brightness over covered pixels (0..255), covered pixel count}.
struct RenderResult
{
  double brightness;
  long coverage;
};

static RenderResult renderNormals(
    ANARIDevice dev, bool authorNormals, float nx, const float ldir[3])
{
  float verts[12] = {
      -0.8f, -0.8f, 0.f, 0.8f, -0.8f, 0.f, 0.8f, 0.8f, 0.f, -0.8f, 0.8f, 0.f};
  ANARIArray1D pos =
      anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  ANARIGeometry g = anariNewGeometry(dev, "quad");
  anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);

  ANARIArray1D norm = nullptr;
  float normals[12] = {nx, 0.f, 0.f, nx, 0.f, 0.f, nx, 0.f, 0.f, nx, 0.f, 0.f};
  if (authorNormals) {
    norm =
        anariNewArray1D(dev, normals, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
    anariSetParameter(dev, g, g_normalParameter, ANARI_ARRAY1D, &norm);
  }
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
  float irradiance = 2.4f;
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
  // Linear channel type: measure radiometric brightness, not sRGB-curved.
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
  const uint32_t *pixels =
      (const uint32_t *)anariMapFrame(dev, frame, "channel.color", &w, &h, &pt);
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
  if (norm)
    anariRelease(dev, norm);
  anariRelease(dev, pos);
  return {n ? sum / n : 0, n};
}

// Render a quad with an authored vertex.normal, then mutate that normal buffer
// in place (map -> flip every normal from +X to -X -> unmap) and re-render the
// same scene. Writes the two brightnesses out. Exercises the re-author path: an
// in-place anariUnmapArray must re-commit the geometry and re-author the copied
// `normals` primvar — the mesh is non-incremental once it carries normals, so
// the change takes a full rebuild rather than the points fast path.
static void renderNormalMutation(ANARIDevice dev, double &before, double &after)
{
  float verts[12] = {
      -0.8f, -0.8f, 0.f, 0.8f, -0.8f, 0.f, 0.8f, 0.8f, 0.f, -0.8f, 0.8f, 0.f};
  ANARIArray1D pos =
      anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  ANARIGeometry g = anariNewGeometry(dev, "quad");
  anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);

  float normals[12] = {
      1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f};
  ANARIArray1D norm =
      anariNewArray1D(dev, normals, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  anariSetParameter(dev, g, g_normalParameter, ANARI_ARRAY1D, &norm);
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
  float ldir[3] = {-1.f, 0.f, 0.f}; // side-light toward -X; +X normal faces it
  float irradiance = 2.4f;
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
  ANARIDataType colorFmt = ANARI_UFIXED8_VEC4;
  anariSetParameter(dev, frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(dev, frame, "channel.color", ANARI_DATA_TYPE, &colorFmt);
  anariSetParameter(dev, frame, "world", ANARI_WORLD, &world);
  anariSetParameter(dev, frame, "camera", ANARI_CAMERA, &cam);
  anariSetParameter(dev, frame, "renderer", ANARI_RENDERER, &ren);
  anariCommitParameters(dev, frame);

  auto measure = [&]() -> double {
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
    return n ? sum / n : 0;
  };

  before = measure(); // +X normal faces the light -> bright

  // In-place mutation: flip every normal to -X, then unmap to re-commit.
  float *mapped = (float *)anariMapArray(dev, norm);
  for (int i = 0; i < 4; ++i)
    mapped[3 * i + 0] = -1.f;
  anariUnmapArray(dev, norm);

  after = measure(); // -X normal faces away -> dark

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
  anariRelease(dev, norm);
  anariRelease(dev, pos);
}

int main(int argc, char **argv)
{
  const char *subtype = argc > 1 ? argv[1] : "storm";
  if (argc > 2 && std::strcmp(argv[2], "faceVarying") == 0)
    g_normalParameter = "faceVarying.normal";
  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  if (!lib)
    return 2;
  ANARIDevice dev = anariNewDevice(lib, subtype);
  if (!dev)
    return 2;

  std::printf("normal parameter: %s\n", g_normalParameter);

  // Two renders varying ONLY the supplied normals, under a side-light traveling
  // toward -X (so a +X surface normal faces it). The geometric normal is +Z,
  // for which N·L == 0 — any brightness split is due solely to the authored
  // normals.
  const float sideLight[3] = {-1.f, 0.f, 0.f};
  const RenderResult toward = renderNormals(dev, true, +1.f, sideLight);
  const RenderResult away = renderNormals(dev, true, -1.f, sideLight);
  std::printf("side-light brightness: normal +X=%.1f, normal -X=%.1f\n",
      toward.brightness,
      away.brightness);

  // App-provided normals reach shading: the +X-facing normal is bright, the
  // -X-facing one dark (clamped max(N·L,0)). Fails today (both fall back to the
  // geometric +Z normal, N·L==0, so the two are equal).
  check(toward.brightness > away.brightness + 10,
      "authored normals drive shading (+X bright, -X dark)");

  // The presence-gate leaves the absent-normal path intact: with no
  // vertex.normal the quad still renders a visible surface under a front-facing
  // light (the delegate's flat +Z fallback is lit here — no assert on its
  // shading value, only that geometry authoring wasn't broken by the gate).
  const float frontLight[3] = {0.f, 0.f, -1.f}; // lights the geometric +Z
  const RenderResult none = renderNormals(dev, false, 0.f, frontLight);
  check(none.coverage > 0,
      "quad with no vertex.normal still renders a visible surface");

  // In-place mutation re-authors: flipping the normal buffer +X -> -X on a live
  // scene must re-shade the quad from bright to dark. Fails if the mesh wrongly
  // kept the points fast path and left the stale normals copy.
  double mutBefore = 0, mutAfter = 0;
  renderNormalMutation(dev, mutBefore, mutAfter);
  std::printf("in-place mutation brightness: +X=%.1f -> -X=%.1f\n",
      mutBefore,
      mutAfter);
  check(mutBefore > mutAfter + 10,
      "in-place normal mutation re-authors the normals primvar");

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
      g_errors);
  return g_errors == 0 ? 0 : 1;
}
