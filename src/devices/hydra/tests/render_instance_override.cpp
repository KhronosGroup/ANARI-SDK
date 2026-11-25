// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises per-instance material/attribute overrides: one group (a single
// quad) is instanced twice with different `instance.color` values, and the
// shared matte material binds its `color` to the ANARI attribute STRING
// "color". The prototype geometry carries no per-vertex color, so each
// instance's own `instance.color` supplies the attribute read — the device
// authors an instance-rate `color` primvar on the (world, group) instancer and
// the existing UsdPrimvarReader picks it up per instance.
//
// The two instances are translated to opposite screen halves; the test asserts
// the left half's dominant channel is RED and the right half's is GREEN. A
// single shared material with no per-instance data would paint both halves the
// same color, so distinct-per-half dominance can only come from the override.
// Relative (dominant channel), not absolute (exact RGB), to stay delegate- and
// tone-map-agnostic.
//
// Later sections cover per-surface constant fallbacks, sampler coordinates on
// the UsdTransform2d and baked paths, transform samplers on instance-only
// attributes (instance-rate derived primvars), and worldPosition on instanced
// surfaces (per-instance copies, with their instance IDs and overrides).
//
// HdEmbree has no material sprim, so attribute- and sampler-driven colors
// degrade to the displayColor fallback there (README.md). On embree every
// edit still renders, and each probe asserts coverage instead of the color;
// the instance-ID check holds on both delegates.

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

// Mean RGB over a column band [x0,x1), counting only covered (non-clear)
// pixels.
static void meanColor(const uint32_t *px,
    uint32_t w,
    uint32_t h,
    uint32_t x0,
    uint32_t x1,
    double &r,
    double &g,
    double &b,
    int &covered)
{
  double sr = 0, sg = 0, sb = 0;
  covered = 0;
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = x0; x < x1; ++x) {
      const uint8_t *p = (const uint8_t *)(px + (y * w + x));
      if (p[0] > 40 || p[1] > 40 || p[2] > 40) {
        sr += p[0];
        sg += p[1];
        sb += p[2];
        ++covered;
      }
    }
  }
  if (covered) {
    r = sr / covered;
    g = sg / covered;
    b = sb / covered;
  } else
    r = g = b = 0;
}

int main(int argc, char **argv)
{
  const char *subtype = argc > 1 ? argv[1] : "storm";
  const bool isEmbree = std::strcmp(subtype, "embree") == 0;
  if (isEmbree)
    std::printf("embree: color probes check coverage only\n");
  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  if (!lib)
    return 2;
  ANARIDevice dev = anariNewDevice(lib, subtype);
  if (!dev)
    return 2;

  // A quad prototype spanning local [-0.5, 0.5] in x/y at z=0.
  float verts[12] = {
      -0.5f, -0.5f, 0.f, 0.5f, -0.5f, 0.f, 0.5f, 0.5f, 0.f, -0.5f, 0.5f, 0.f};
  ANARIArray1D pos =
      anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  ANARIGeometry geom = anariNewGeometry(dev, "quad");
  anariSetParameter(dev, geom, "vertex.position", ANARI_ARRAY1D, &pos);
  const float geometryBlue[4] = {0, 0, 1, 1};
  anariSetParameter(dev, geom, "color", ANARI_FLOAT32_VEC4, geometryBlue);
  anariCommitParameters(dev, geom);

  // matte, color bound to the ANARI attribute "color". No constant color and no
  // per-vertex color -> the per-instance override is the source.
  ANARIMaterial mat = anariNewMaterial(dev, "matte");
  const char *attr = "color";
  anariSetParameter(dev, mat, "color", ANARI_STRING, attr);
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

  // Two instances of the one group: left (red), right (green). Column-major 4x4
  // with the translation in column 3.
  const float tx[2] = {-0.75f, 0.75f};
  const float col[2][4] = {{1.f, 0.f, 0.f, 1.f}, {0.f, 1.f, 0.f, 1.f}};
  std::vector<ANARIInstance> instances;
  for (int i = 0; i < 2; ++i) {
    float xfm[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, tx[i], 0, 0, 1};
    ANARIInstance inst = anariNewInstance(dev, "transform");
    anariSetParameter(dev, inst, "group", ANARI_GROUP, &group);
    anariSetParameter(dev, inst, "transform", ANARI_FLOAT32_MAT4, xfm);
    anariSetParameter(dev, inst, "color", ANARI_FLOAT32_VEC4, col[i]);
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
  float fovy = 0.7f;
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
  const uint32_t *px =
      (const uint32_t *)anariMapFrame(dev, frame, "channel.color", &w, &h, &pt);

  double lr = 0, lg = 0, lb = 0, rr = 0, rg = 0, rb = 0;
  int lcov = 0, rcov = 0;
  if (px) {
    // Sample the outer thirds to avoid the seam between the two instances.
    meanColor(px, w, h, 0, w / 3, lr, lg, lb, lcov);
    meanColor(px, w, h, 2 * w / 3, w, rr, rg, rb, rcov);
  }
  anariUnmapFrame(dev, frame, "channel.color");

  std::printf(
      "left  third: cov=%d mean rgb=(%.0f,%.0f,%.0f)\n", lcov, lr, lg, lb);
  std::printf(
      "right third: cov=%d mean rgb=(%.0f,%.0f,%.0f)\n", rcov, rr, rg, rb);

  check(lcov > 300, "left instance covers its screen region");
  check(rcov > 300, "right instance covers its screen region");
  if (!isEmbree) {
    check(lr > lg + 30 && lr > lb + 30,
        "left instance is RED (its own instance.color)");
    check(rg > rr + 30 && rg > rb + 30,
        "right instance is GREEN (its own instance.color)");
  }

  // Two prototypes have distinct constant fallbacks; the right instance leaves
  // its override unset. Each prototype must inherit its own fallback.
  float lower[12] = {-.5f, -.8f, 0, .5f, -.8f, 0, .5f, -.2f, 0, -.5f, -.2f, 0};
  float upper[12] = {-.5f, .2f, 0, .5f, .2f, 0, .5f, .8f, 0, -.5f, .8f, 0};
  auto lowerPositions =
      anariNewArray1D(dev, lower, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  auto upperPositions =
      anariNewArray1D(dev, upper, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  anariSetParameter(
      dev, geom, "vertex.position", ANARI_ARRAY1D, &lowerPositions);
  anariCommitParameters(dev, geom);
  auto upperGeometry = anariNewGeometry(dev, "quad");
  const float yellow[4] = {1, 1, 0, 1};
  anariSetParameter(
      dev, upperGeometry, "vertex.position", ANARI_ARRAY1D, &upperPositions);
  anariSetParameter(dev, upperGeometry, "color", ANARI_FLOAT32_VEC4, yellow);
  anariCommitParameters(dev, upperGeometry);
  auto upperSurface = anariNewSurface(dev);
  anariSetParameter(
      dev, upperSurface, "geometry", ANARI_GEOMETRY, &upperGeometry);
  anariSetParameter(dev, upperSurface, "material", ANARI_MATERIAL, &mat);
  anariCommitParameters(dev, upperSurface);
  const ANARISurface both[] = {surf, upperSurface};
  auto bothSurfaces =
      anariNewArray1D(dev, both, nullptr, nullptr, ANARI_SURFACE, 2);
  anariSetParameter(dev, group, "surface", ANARI_ARRAY1D, &bothSurfaces);
  anariCommitParameters(dev, group);
  anariUnsetParameter(dev, instances[1], "color");
  anariCommitParameters(dev, instances[1]);
  // Probe one (instance, prototype) region; channel 3 means yellow.
  auto sampleAt = [&](bool left, bool top, int channel, const char *message) {
    anariRenderFrame(dev, frame);
    anariFrameReady(dev, frame, ANARI_WAIT);
    px = (const uint32_t *)anariMapFrame(
        dev, frame, "channel.color", &w, &h, &pt);
    bool pass = false;
    if (px) {
      const uint8_t *p = (const uint8_t *)(px + (top ? 3 * h / 4 : h / 4) * w
          + (left ? w / 6 : 5 * w / 6));
      if (isEmbree)
        pass = p[0] > 40 || p[1] > 40 || p[2] > 40;
      else
        pass = channel == 3 ? p[0] > p[2] + 30 && p[1] > p[2] + 30
                            : p[channel] > p[(channel + 1) % 3] + 30
                && p[channel] > p[(channel + 2) % 3] + 30;
      if (!pass)
        std::printf("    probe rgb=(%d,%d,%d)\n", p[0], p[1], p[2]);
    }
    anariUnmapFrame(dev, frame, "channel.color");
    check(pass, message);
  };
  auto sample = [&](bool top, int channel, const char *message) {
    sampleAt(false, top, channel, message);
  };
  sample(false, 2, "unset instance uses lower prototype blue constant");
  sample(true, 3, "unset instance uses upper prototype yellow constant");
  const float green[4] = {0, 1, 0, 1};
  anariSetParameter(dev, geom, "color", ANARI_FLOAT32_VEC4, green);
  anariCommitParameters(dev, geom);
  sample(false, 1, "constant edit refreshes per-prototype instance fallback");
  anariUnsetParameter(dev, instances[0], "color");
  anariCommitParameters(dev, instances[0]);
  sample(
      false, 1, "removing all overrides restores grouped constant prototype");
  sample(true, 3, "prototype constants stay distinct after instancer merge");
  anariSetParameter(dev, instances[0], "color", ANARI_FLOAT32_VEC4, col[0]);
  anariCommitParameters(dev, instances[0]);
  sample(false, 1, "restoring override re-splits constant fallback instancers");
  // Texture coordinates use the same attribute0 precedence as material reads.
  const float uvLeft[4] = {.25f, .5f, 0, 1};
  const float uvRight[4] = {.75f, .5f, 0, 1};
  anariSetParameter(dev, geom, "attribute0", ANARI_FLOAT32_VEC4, uvLeft);
  anariCommitParameters(dev, geom);
  anariSetParameter(
      dev, upperGeometry, "attribute0", ANARI_FLOAT32_VEC4, uvRight);
  anariCommitParameters(dev, upperGeometry);
  anariSetParameter(
      dev, instances[0], "attribute0", ANARI_FLOAT32_VEC4, uvRight);
  anariCommitParameters(dev, instances[0]);
  const float texels[] = {0, 0, 1, 0, 1, 0};
  auto image =
      anariNewArray2D(dev, texels, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2, 1);
  auto sampler = anariNewSampler(dev, "image2D");
  anariSetParameter(dev, sampler, "image", ANARI_ARRAY2D, &image);
  anariSetParameter(dev, sampler, "filter", ANARI_STRING, "nearest");
  anariCommitParameters(dev, sampler);
  anariSetParameter(dev, mat, "color", ANARI_SAMPLER, &sampler);
  anariCommitParameters(dev, mat);
  sample(false, 2, "unset instance samples lower geometry attribute0 constant");
  sample(true, 1, "unset instance samples distinct upper attribute0 constant");
  sampleAt(
      true, false, 1, "instance attribute0 override beats the lower constant");

  // A shear inTransform takes the baked st_s{N} path; the instance override
  // must still win over the geometry constant (011-2). Only u matters: the
  // image is one row.
  const float shear[16] = {1, .5f, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  anariSetParameter(dev, sampler, "inTransform", ANARI_FLOAT32_MAT4, shear);
  anariCommitParameters(dev, sampler);
  sampleAt(false, false, 2, "baked: unset instance samples the lower constant");
  sampleAt(false, true, 1, "baked: unset instance samples the upper constant");
  sampleAt(true, false, 1, "baked: instance override beats the lower constant");
  sampleAt(true, true, 1, "baked: instance override on the upper prototype");

  // Transform samplers on an instance-only attribute (013-3): no geometry
  // constants and no other overrides, so both prototypes share the group
  // instancer, and the geometry lacks attribute1 entirely.
  for (auto g : {geom, upperGeometry}) {
    anariUnsetParameter(dev, g, "attribute0");
    anariUnsetParameter(dev, g, "color");
    anariCommitParameters(dev, g);
  }
  anariUnsetParameter(dev, instances[0], "attribute0");
  anariUnsetParameter(dev, instances[0], "color");
  const float red[4] = {1, 0, 0, 1};
  const float blue[4] = {0, 0, 1, 1};
  anariSetParameter(dev, instances[0], "attribute1", ANARI_FLOAT32_VEC4, red);
  anariCommitParameters(dev, instances[0]);
  anariSetParameter(dev, instances[1], "attribute1", ANARI_FLOAT32_VEC4, blue);
  anariCommitParameters(dev, instances[1]);
  auto xf = anariNewSampler(dev, "transform");
  anariSetParameter(dev, xf, "inAttribute", ANARI_STRING, "attribute1");
  anariCommitParameters(dev, xf);
  anariSetParameter(dev, mat, "color", ANARI_SAMPLER, &xf);
  anariCommitParameters(dev, mat);
  sampleAt(
      true, false, 0, "transform: left instance reads its attribute1 (lower)");
  sampleAt(
      true, true, 0, "transform: left instance reads its attribute1 (upper)");
  sampleAt(false,
      false,
      2,
      "transform: right instance reads its attribute1 (lower)");
  sampleAt(
      false, true, 2, "transform: right instance reads its attribute1 (upper)");
  // A geometry constant fills instances that leave the override unset.
  anariSetParameter(dev, geom, "attribute1", ANARI_FLOAT32_VEC4, green);
  anariCommitParameters(dev, geom);
  anariUnsetParameter(dev, instances[1], "attribute1");
  anariCommitParameters(dev, instances[1]);
  sampleAt(true, false, 0, "transform: override beats the geometry constant");
  sampleAt(false, false, 1, "transform: unset instance falls to the constant");
  // A sampler edit re-authors the per-instance values: swap red and blue.
  const float swapRB[16] = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1};
  anariSetParameter(dev, xf, "outTransform", ANARI_FLOAT32_MAT4, swapRB);
  anariCommitParameters(dev, xf);
  sampleAt(
      true, false, 2, "transform: sampler edit reaches the instance values");
  anariUnsetParameter(dev, xf, "outTransform");
  anariCommitParameters(dev, xf);

  // worldPosition on an instanced surface (013-1): color = (0.5 + 0.4x,
  // 0.5 - 0.4x, 0). At each instance's centre world x = -/+0.75, so the left
  // instance is green and the right red; object space (x = 0) would be yellow.
  const float worldX[16] = {
      .4f, -.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  const float half[4] = {.5f, .5f, 0, 0};
  anariSetParameter(dev, xf, "inAttribute", ANARI_STRING, "worldPosition");
  anariSetParameter(dev, xf, "outTransform", ANARI_FLOAT32_MAT4, worldX);
  anariSetParameter(dev, xf, "outOffset", ANARI_FLOAT32_VEC4, half);
  anariCommitParameters(dev, xf);
  sampleAt(
      true, false, 1, "worldPosition: left instance reads world x (lower)");
  sampleAt(true, true, 1, "worldPosition: left instance reads world x (upper)");
  sampleAt(
      false, false, 0, "worldPosition: right instance reads world x (lower)");
  sampleAt(
      false, true, 0, "worldPosition: right instance reads world x (upper)");

  // The per-instance copies keep instance IDs and attribute overrides: color
  // reads attribute1 while a world-space opacity sampler (always 1) forces
  // the expansion.
  const uint32_t instanceIds[2] = {41, 42};
  for (int i = 0; i < 2; ++i) {
    anariSetParameter(dev, instances[i], "id", ANARI_UINT32, &instanceIds[i]);
    anariCommitParameters(dev, instances[i]);
  }
  ANARIDataType u32 = ANARI_UINT32;
  anariSetParameter(dev, frame, "channel.instanceId", ANARI_DATA_TYPE, &u32);
  anariCommitParameters(dev, frame);
  anariRenderFrame(dev, frame);
  anariFrameReady(dev, frame, ANARI_WAIT);
  const uint32_t *ids = (const uint32_t *)anariMapFrame(
      dev, frame, "channel.instanceId", &w, &h, &pt);
  check(ids && ids[h / 4 * w + w / 6] == 41
          && ids[3 * h / 4 * w + 5 * w / 6] == 42,
      "worldPosition: per-instance copies report their instance IDs");
  if (ids)
    std::printf("    ids: left=%u right=%u\n",
        ids[h / 4 * w + w / 6],
        ids[3 * h / 4 * w + 5 * w / 6]);
  anariUnmapFrame(dev, frame, "channel.instanceId");
  const float zero[16] = {};
  const float one[4] = {1, 0, 0, 0};
  anariSetParameter(dev, xf, "outTransform", ANARI_FLOAT32_MAT4, zero);
  anariSetParameter(dev, xf, "outOffset", ANARI_FLOAT32_VEC4, one);
  anariCommitParameters(dev, xf);
  anariSetParameter(dev, mat, "opacity", ANARI_SAMPLER, &xf);
  anariSetParameter(dev, mat, "color", ANARI_STRING, "attribute1");
  anariCommitParameters(dev, mat);
  sampleAt(true, false, 0, "worldPosition: copy applies the instance override");
  sampleAt(
      false, false, 1, "worldPosition: copy falls to the geometry constant");
  anariRelease(dev, xf);
  anariRelease(dev, sampler);
  anariRelease(dev, image);
  anariRelease(dev, bothSurfaces);
  anariRelease(dev, upperSurface);
  anariRelease(dev, upperGeometry);
  anariRelease(dev, lowerPositions);
  anariRelease(dev, upperPositions);

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

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
      g_errors);
  return g_errors == 0 ? 0 : 1;
}
