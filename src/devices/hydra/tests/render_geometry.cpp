// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises each ANARI geometry subtype (SPEC §4.1) end-to-end through the C
// API: triangle/quad -> mesh, sphere -> tessellated mesh, curve -> basisCurves,
// cone/cylinder -> commit-time tessellated mesh. Each subtype is rendered in
// its own world via a Hydra delegate (default: storm) and the frame is read
// back; a non-trivial covered-pixel area proves the prim reached the delegate.

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

// Render `surf` alone in a fresh world and return the covered-pixel count.
// When `midStrip` is non-null, it receives the covered count within the middle
// vertical strip (x in [3w/8, 5w/8)) — used to prove disjoint curve segments
// are not bridged. When `leftHalf`/`rightHalf` are non-null, they receive the
// covered count in x < w/2 and x >= w/2 respectively — used to prove a
// per-segment value (radius, cap) landed on the correct side, not swapped.
// When `centerColor` is non-null, it receives the RGB of the exact center
// pixel — used with a per-endpoint vertex.color to prove which physical
// endpoint's cap fan (its center vertex carries that endpoint's own color) is
// the one facing the camera, since a filled-vs-open pixel count alone can't
// tell two capped endpoints apart (either one blocks the same view ray).
static int renderCoverage(ANARIDevice dev,
    ANARISurface surf,
    int *midStrip = nullptr,
    int *leftHalf = nullptr,
    int *rightHalf = nullptr,
    uint8_t centerColor[3] = nullptr)
{
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
  int coverage = 0;
  if (midStrip)
    *midStrip = 0;
  if (leftHalf)
    *leftHalf = 0;
  if (rightHalf)
    *rightHalf = 0;
  if (pixels) {
    for (uint32_t i = 0; i < w * h; ++i) {
      const uint8_t *p = (const uint8_t *)(pixels + i);
      if (p[0] > 40 || p[1] > 40 || p[2] > 40) {
        ++coverage;
        const uint32_t x = i % w;
        if (midStrip && x >= 3 * w / 8 && x < 5 * w / 8)
          ++*midStrip;
        if (leftHalf && x < w / 2)
          ++*leftHalf;
        if (rightHalf && x >= w / 2)
          ++*rightHalf;
      }
    }
    if (centerColor) {
      const uint8_t *p = (const uint8_t *)(pixels + (h / 2) * w + w / 2);
      centerColor[0] = p[0];
      centerColor[1] = p[1];
      centerColor[2] = p[2];
    }
  }
  anariUnmapFrame(dev, frame, "channel.color");

  anariRelease(dev, frame);
  anariRelease(dev, ren);
  anariRelease(dev, cam);
  anariRelease(dev, surfArray);
  anariRelease(dev, world);
  return coverage;
}

static ANARISurface makeSurface(
    ANARIDevice dev, ANARIGeometry geom, const float color[3])
{
  ANARIMaterial mat = anariNewMaterial(dev, "matte");
  anariSetParameter(dev, mat, "color", ANARI_FLOAT32_VEC3, color);
  anariCommitParameters(dev, mat);
  ANARISurface surf = anariNewSurface(dev);
  anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &geom);
  anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
  anariCommitParameters(dev, surf);
  anariRelease(dev, mat);
  return surf;
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
  if (!dev) {
    std::printf("FAIL: device\n");
    return 2;
  }

  const float color[3] = {0.8f, 0.6f, 0.2f};

  // --- quad -> mesh ----------------------------------------------------------
  {
    float verts[12] = {
        -0.7f, -0.7f, 0.f, 0.7f, -0.7f, 0.f, 0.7f, 0.7f, 0.f, -0.7f, 0.7f, 0.f};
    ANARIArray1D pos =
        anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
    ANARIGeometry g = anariNewGeometry(dev, "quad");
    anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
    anariCommitParameters(dev, g);
    ANARISurface s = makeSurface(dev, g, color);
    int cov = renderCoverage(dev, s);
    std::printf("quad coverage: %d\n", cov);
    check(cov > 2000, "quad renders a filled area");
    anariRelease(dev, s);
    anariRelease(dev, g);
    anariRelease(dev, pos);
  }

  // --- sphere -> tessellated mesh
  // ------------------------------------------------------
  {
    float verts[3] = {0.f, 0.f, 0.f};
    float radius = 0.6f;
    ANARIArray1D pos =
        anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 1);
    ANARIGeometry g = anariNewGeometry(dev, "sphere");
    anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
    anariSetParameter(dev, g, "radius", ANARI_FLOAT32, &radius);
    anariCommitParameters(dev, g);
    ANARISurface s = makeSurface(dev, g, color);
    int cov = renderCoverage(dev, s);
    std::printf("sphere coverage: %d\n", cov);
    check(cov > 500, "sphere (tessellated mesh) renders a filled area");
    anariRelease(dev, s);
    anariRelease(dev, g);
    anariRelease(dev, pos);
  }

  // --- curve -> basisCurves --------------------------------------------------
  {
    float verts[12] = {
        -0.8f, -0.5f, 0.f, -0.2f, 0.5f, 0.f, 0.2f, -0.5f, 0.f, 0.8f, 0.5f, 0.f};
    float radii[4] = {0.1f, 0.1f, 0.1f, 0.1f};
    ANARIArray1D pos =
        anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
    ANARIArray1D rad =
        anariNewArray1D(dev, radii, nullptr, nullptr, ANARI_FLOAT32, 4);
    ANARIGeometry g = anariNewGeometry(dev, "curve");
    anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
    anariSetParameter(dev, g, "vertex.radius", ANARI_ARRAY1D, &rad);
    anariCommitParameters(dev, g);
    ANARISurface s = makeSurface(dev, g, color);
    int cov = renderCoverage(dev, s);
    std::printf("curve coverage: %d\n", cov);
    check(cov > 300, "curve (basisCurves) renders a filled area");
    anariRelease(dev, s);
    anariRelease(dev, g);
    anariRelease(dev, rad);
    anariRelease(dev, pos);
  }

  // --- indexed multi-segment curve
  // -------------------------------------------- ANARI curve: primitive.index
  // selects each segment's first vertex, so index [0,2] over 4 vertices means
  // two disjoint segments (0,1) and (2,3) — two vertical tubes at x=±0.6 with
  // nothing in between. A polyline that ignored the index would bridge (1,2)
  // straight through the image center.
  {
    float verts[12] = {
        -0.6f, -0.5f, 0.f, -0.6f, 0.5f, 0.f, 0.6f, -0.5f, 0.f, 0.6f, 0.5f, 0.f};
    uint32_t segStart[2] = {0, 2};
    float radius = 0.1f;
    ANARIArray1D pos =
        anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
    ANARIArray1D idx =
        anariNewArray1D(dev, segStart, nullptr, nullptr, ANARI_UINT32, 2);
    ANARIGeometry g = anariNewGeometry(dev, "curve");
    anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
    anariSetParameter(dev, g, "primitive.index", ANARI_ARRAY1D, &idx);
    anariSetParameter(dev, g, "radius", ANARI_FLOAT32, &radius);
    anariCommitParameters(dev, g);
    ANARISurface s = makeSurface(dev, g, color);
    int mid = 0;
    int cov = renderCoverage(dev, s, &mid);
    std::printf("indexed curve coverage: %d (mid strip: %d)\n", cov, mid);
    check(cov > 300, "indexed curve renders both segments");
    check(mid < 20, "indexed curve segments are disjoint (no bridge, §4.1)");
    anariRelease(dev, s);
    anariRelease(dev, g);
    anariRelease(dev, idx);
    anariRelease(dev, pos);
  }

  // --- cone -> tessellated mesh ----------------------------------------------
  {
    float verts[6] = {0.f, -0.7f, 0.f, 0.f, 0.7f, 0.f};
    float radii[2] = {0.5f, 0.f};
    ANARIArray1D pos =
        anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
    ANARIArray1D rad =
        anariNewArray1D(dev, radii, nullptr, nullptr, ANARI_FLOAT32, 2);
    ANARIGeometry g = anariNewGeometry(dev, "cone");
    anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
    anariSetParameter(dev, g, "vertex.radius", ANARI_ARRAY1D, &rad);
    anariCommitParameters(dev, g);
    ANARISurface s = makeSurface(dev, g, color);
    int cov = renderCoverage(dev, s);
    std::printf("cone coverage: %d\n", cov);
    check(cov > 1000, "cone (tessellated mesh) renders a filled area");
    anariRelease(dev, s);
    anariRelease(dev, g);
    anariRelease(dev, rad);
    anariRelease(dev, pos);
  }

  // --- cylinder -> tessellated mesh ------------------------------------------
  {
    float verts[6] = {-0.5f, 0.f, 0.f, 0.5f, 0.f, 0.f};
    float radius = 0.4f;
    ANARIArray1D pos =
        anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
    ANARIGeometry g = anariNewGeometry(dev, "cylinder");
    anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
    anariSetParameter(dev, g, "radius", ANARI_FLOAT32, &radius);
    anariCommitParameters(dev, g);
    ANARISurface s = makeSurface(dev, g, color);
    int cov = renderCoverage(dev, s);
    std::printf("cylinder coverage: %d\n", cov);
    check(cov > 1000, "cylinder (tessellated mesh) renders a filled area");
    anariRelease(dev, s);
    anariRelease(dev, g);
    anariRelease(dev, pos);
  }

  // --- capped cylinder, end-on
  // ------------------------------------------------- Axis along the view
  // direction: the open tube's silhouette is only the thin annulus between the
  // near and far rims (~4k px here); the "both" caps fill the full near-rim
  // disk (~16k px).
  {
    float verts[6] = {0.f, 0.f, -0.2f, 0.f, 0.f, 0.2f};
    float radius = 0.4f;
    ANARIArray1D pos =
        anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
    ANARIGeometry g = anariNewGeometry(dev, "cylinder");
    anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
    anariSetParameter(dev, g, "radius", ANARI_FLOAT32, &radius);
    anariSetParameter(dev, g, "caps", ANARI_STRING, "both");
    anariCommitParameters(dev, g);
    ANARISurface s = makeSurface(dev, g, color);
    int cov = renderCoverage(dev, s);
    std::printf("capped cylinder (end-on) coverage: %d\n", cov);
    check(cov > 10000, "cylinder caps fill the end disk (§4.1)");
    anariRelease(dev, s);
    anariRelease(dev, g);
    anariRelease(dev, pos);
  }

  // --- capped truncated cone, end-on
  // -------------------------------------------- vertex.radius tapers 0.5 ->
  // 0.4 (both > 0, so both ends earn a cap); the wide end faces the camera.
  // Open: an annulus (~13k px, the hole shows through). Capped: the full wide
  // disk (~25k px).
  {
    float verts[6] = {0.f, 0.f, 0.2f, 0.f, 0.f, -0.2f};
    float radii[2] = {0.5f, 0.4f};
    ANARIArray1D pos =
        anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
    ANARIArray1D rad =
        anariNewArray1D(dev, radii, nullptr, nullptr, ANARI_FLOAT32, 2);
    ANARIGeometry g = anariNewGeometry(dev, "cone");
    anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
    anariSetParameter(dev, g, "vertex.radius", ANARI_ARRAY1D, &rad);
    anariSetParameter(dev, g, "caps", ANARI_STRING, "both");
    anariCommitParameters(dev, g);
    ANARISurface s = makeSurface(dev, g, color);
    int cov = renderCoverage(dev, s);
    std::printf("capped cone (end-on) coverage: %d\n", cov);
    check(cov > 20000, "cone caps fill the wide-end disk (§4.1)");
    anariRelease(dev, s);
    anariRelease(dev, g);
    anariRelease(dev, rad);
    anariRelease(dev, pos);
  }

  // --- indexed cylinder with per-primitive radii
  // ------------------------------ Two segments share one primitive.index
  // array, one sitting in the left half of the frame and one in the right;
  // primitive.radius (SPEC KHR_GEOMETRY_CYLINDER) gives them very different
  // widths. Swapping which array slot holds the wide radius between the two
  // passes swaps which half gets more coverage — a coverage-total-only check
  // can't catch primitive.radius being misindexed (e.g. read per-vertex, or
  // reversed), but this asymmetry can.
  {
    float verts[12] = {
        -0.6f, -0.4f, 0.f, -0.6f, 0.4f, 0.f, 0.6f, -0.4f, 0.f, 0.6f, 0.4f, 0.f};
    uint32_t segIdx[4] = {0, 1, 2, 3};
    for (int pass = 0; pass < 2; ++pass) {
      // pass 0: left segment wide, right narrow. pass 1: swapped.
      float radii[2] = {pass == 0 ? 0.35f : 0.08f, pass == 0 ? 0.08f : 0.35f};
      ANARIArray1D pos =
          anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
      ANARIArray1D idx =
          anariNewArray1D(dev, segIdx, nullptr, nullptr, ANARI_UINT32_VEC2, 2);
      ANARIArray1D rad =
          anariNewArray1D(dev, radii, nullptr, nullptr, ANARI_FLOAT32, 2);
      ANARIGeometry g = anariNewGeometry(dev, "cylinder");
      anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
      anariSetParameter(dev, g, "primitive.index", ANARI_ARRAY1D, &idx);
      anariSetParameter(dev, g, "primitive.radius", ANARI_ARRAY1D, &rad);
      anariCommitParameters(dev, g);
      ANARISurface s = makeSurface(dev, g, color);
      int left = 0, right = 0;
      renderCoverage(dev, s, nullptr, &left, &right);
      std::printf(
          "indexed cylinder pass %d: left=%d right=%d\n", pass, left, right);
      check((pass == 0 ? left > right : right > left),
          "primitive.radius lands on the correct segment via primitive.index (§4.1)");
      anariRelease(dev, s);
      anariRelease(dev, g);
      anariRelease(dev, rad);
      anariRelease(dev, idx);
      anariRelease(dev, pos);
    }
  }

  // --- vertex.cap indexes seg.first/seg.second correctly, not swapped --------
  // A capped-vs-open pixel count can't tell "p0 capped" from "p1 capped" apart:
  // for a constant-radius end-on cylinder, either single cap blocks the same
  // straight-through view ray and fills the same silhouette. Instead, give the
  // two endpoints distinct vertex.color values; the cap fan's center vertex
  // carries its own endpoint's color (TubeMesh.cpp emitCap: `flip ? seg.first :
  // seg.second`), so the exact center pixel's color reveals which physical
  // endpoint produced the cap that's facing the camera.
  {
    float verts[6] = {0.f, 0.f, -0.2f, 0.f, 0.f, 0.2f}; // p0 far, p1 near
    float radius = 0.4f;
    const float farRed[4] = {1.f, 0.f, 0.f, 1.f};
    const float nearGreen[4] = {0.f, 1.f, 0.f, 1.f};
    float colors[8];
    std::memcpy(colors, farRed, sizeof(farRed));
    std::memcpy(colors + 4, nearGreen, sizeof(nearGreen));
    uint8_t capMasks[2][2] = {
        {1, 0}, {0, 1}}; // cap far(p0) only; cap near(p1) only
    for (int i = 0; i < 2; ++i) {
      ANARIArray1D pos =
          anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
      ANARIArray1D vcolor =
          anariNewArray1D(dev, colors, nullptr, nullptr, ANARI_FLOAT32_VEC4, 2);
      ANARIArray1D vcap =
          anariNewArray1D(dev, capMasks[i], nullptr, nullptr, ANARI_UINT8, 2);
      ANARIGeometry g = anariNewGeometry(dev, "cylinder");
      anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
      anariSetParameter(dev, g, "vertex.color", ANARI_ARRAY1D, &vcolor);
      anariSetParameter(dev, g, "radius", ANARI_FLOAT32, &radius);
      anariSetParameter(dev, g, "vertex.cap", ANARI_ARRAY1D, &vcap);
      anariCommitParameters(dev, g);
      ANARIMaterial mat = anariNewMaterial(dev, "matte");
      anariSetParameter(dev, mat, "color", ANARI_STRING, "color");
      anariCommitParameters(dev, mat);
      ANARISurface s = anariNewSurface(dev);
      anariSetParameter(dev, s, "geometry", ANARI_GEOMETRY, &g);
      anariSetParameter(dev, s, "material", ANARI_MATERIAL, &mat);
      anariCommitParameters(dev, s);
      uint8_t center[3] = {0, 0, 0};
      renderCoverage(dev, s, nullptr, nullptr, nullptr, center);
      std::printf("vertex.cap=%s center pixel: (%d,%d,%d)\n",
          i == 0 ? "far(p0) only" : "near(p1) only",
          center[0],
          center[1],
          center[2]);
      // Capping only the far endpoint (p0, red) must show red at center;
      // capping only the near endpoint (p1, green) must show green.
      const bool expectRed = i == 0;
      check(expectRed ? (center[0] > center[1] + 40)
                      : (center[1] > center[0] + 40),
          "vertex.cap indexes seg.first/seg.second correctly (§4.1)");
      anariRelease(dev, s);
      anariRelease(dev, mat);
      anariRelease(dev, g);
      anariRelease(dev, vcap);
      anariRelease(dev, vcolor);
      anariRelease(dev, pos);
    }
  }
  {
    float verts[6] = {0.f, 0.f, -0.2f, 0.f, 0.f, 0.2f};
    float radius = 0.4f;
    uint8_t capsOff[2] = {0, 0};
    ANARIArray1D pos =
        anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
    ANARIArray1D vcap =
        anariNewArray1D(dev, capsOff, nullptr, nullptr, ANARI_UINT8, 2);
    ANARIGeometry g = anariNewGeometry(dev, "cylinder");
    anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
    anariSetParameter(dev, g, "radius", ANARI_FLOAT32, &radius);
    anariSetParameter(dev, g, "caps", ANARI_STRING, "both");
    anariSetParameter(dev, g, "vertex.cap", ANARI_ARRAY1D, &vcap);
    anariCommitParameters(dev, g);
    ANARISurface s = makeSurface(dev, g, color);
    int covBefore = renderCoverage(dev, s);
    std::printf("vertex.cap-zeroed cylinder (end-on, caps=both) coverage: %d\n",
        covBefore);
    check(covBefore < 8000,
        "vertex.cap overrides 'caps=both' to leave ends open");

    // In-place vertex.cap edit (SPEC §4.5): re-enabling both ends must
    // re-tessellate the caps back in, without touching 'caps' or re-committing
    // the geometry's other parameters.
    uint8_t *mapped = static_cast<uint8_t *>(anariMapArray(dev, vcap));
    mapped[0] = mapped[1] = 1;
    anariUnmapArray(dev, vcap);
    int covAfter = renderCoverage(dev, s);
    std::printf("vertex.cap edit (re-enabled) coverage: %d\n", covAfter);
    check(covAfter > 10000, "in-place vertex.cap edit re-tessellates the caps");

    anariRelease(dev, s);
    anariRelease(dev, g);
    anariRelease(dev, vcap);
    anariRelease(dev, pos);
  }

  // --- cylinder primitive.radius edit re-tessellates (SPEC §4.5)
  // --------------
  {
    float verts[6] = {-0.5f, 0.f, 0.f, 0.5f, 0.f, 0.f};
    float radius = 0.1f;
    ANARIArray1D pos =
        anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
    ANARIArray1D rad =
        anariNewArray1D(dev, &radius, nullptr, nullptr, ANARI_FLOAT32, 1);
    ANARIGeometry g = anariNewGeometry(dev, "cylinder");
    anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
    anariSetParameter(dev, g, "primitive.radius", ANARI_ARRAY1D, &rad);
    anariCommitParameters(dev, g);
    ANARISurface s = makeSurface(dev, g, color);
    const int covBefore = renderCoverage(dev, s);
    float *mapped = static_cast<float *>(anariMapArray(dev, rad));
    mapped[0] = 0.45f;
    anariUnmapArray(dev, rad);
    const int covAfter = renderCoverage(dev, s);
    std::printf("primitive.radius edit coverage: before=%d after=%d\n",
        covBefore,
        covAfter);
    check(covAfter > covBefore + 2000,
        "in-place primitive.radius edit re-tessellates the cylinder");
    anariRelease(dev, s);
    anariRelease(dev, g);
    anariRelease(dev, rad);
    anariRelease(dev, pos);
  }

  // --- cone vertex.cap forces a cap, but a zero-radius apex is never capped
  // --- Base radius 0.5 faces the camera (z=0.2); apex radius 0.0 sits behind
  // (z=-0.2). vertex.cap=[1,1] forces both ends, but the zero-radius apex has
  // no area to cap regardless: coverage should still match the base-cap-only
  // magnitude from the "capped truncated cone" test above.
  {
    float verts[6] = {0.f, 0.f, 0.2f, 0.f, 0.f, -0.2f};
    float radii[2] = {0.5f, 0.f};
    uint8_t capsOn[2] = {1, 1};
    ANARIArray1D pos =
        anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
    ANARIArray1D rad =
        anariNewArray1D(dev, radii, nullptr, nullptr, ANARI_FLOAT32, 2);
    ANARIArray1D vcap =
        anariNewArray1D(dev, capsOn, nullptr, nullptr, ANARI_UINT8, 2);
    ANARIGeometry g = anariNewGeometry(dev, "cone");
    anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
    anariSetParameter(dev, g, "vertex.radius", ANARI_ARRAY1D, &rad);
    anariSetParameter(dev, g, "vertex.cap", ANARI_ARRAY1D, &vcap);
    anariCommitParameters(dev, g);
    ANARISurface s = makeSurface(dev, g, color);
    int cov = renderCoverage(dev, s);
    std::printf("zero-tip cone with vertex.cap forced coverage: %d\n", cov);
    check(cov > 20000,
        "forcing vertex.cap on a zero-radius tip caps only the base (§4.1)");
    anariRelease(dev, s);
    anariRelease(dev, g);
    anariRelease(dev, vcap);
    anariRelease(dev, rad);
    anariRelease(dev, pos);
  }

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);

  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
      g_errors);
  return g_errors == 0 ? 0 : 1;
}
