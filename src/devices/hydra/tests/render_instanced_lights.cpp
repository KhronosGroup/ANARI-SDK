// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises instanced-light expansion: a group holding ONE point light,
// instanced N times, expands to one light sprim per (instance, light) pair at
// /anari/w{N}/lights/i{I}_l{L}, each with the light's local transform composed
// with its instance transform. N instances of the same light-group therefore
// place N distinct lights in the scene.
//
// Sharp check: a large white quad fills the frame. The group's single point
// light sits just in front of the quad; two instances translate it to +X and
// -X, so instance A lights the right of the quad and instance B lights the
// left. With BOTH instances the left region lights up that a single +X-only
// instance leaves dark — that brightening is the direct fingerprint of
// expansion (a lone, unexpanded light could only light one side).
//
// Shading includes the Lambertian 1/pi and point-light distance falloff, so
// intensities are chosen to clear the coverage/measurement floor; assertions
// are relative (left-lit AND right-lit; two-instance-left > one-instance-left).

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

// Render a frame-filling white quad lit by a light-only group instanced at the
// given X translations. Fills `leftAvg`/`rightAvg` with the mean luminance
// (0..255) over the left / right thirds of the image.
static void renderScene(ANARIDevice dev,
    const std::vector<float> &instanceX,
    double &leftAvg,
    double &rightAvg)
{
  // Large quad facing +Z, covering the whole frame (world-direct receiver).
  float verts[12] = {
      -1.5f, -1.5f, 0.f, 1.5f, -1.5f, 0.f, 1.5f, 1.5f, 0.f, -1.5f, 1.5f, 0.f};
  ANARIArray1D pos =
      anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  ANARIGeometry g = anariNewGeometry(dev, "quad");
  anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
  anariCommitParameters(dev, g);

  ANARIMaterial mat = anariNewMaterial(dev, "matte");
  float color[3] = {0.9f, 0.9f, 0.9f};
  anariSetParameter(dev, mat, "color", ANARI_FLOAT32_VEC3, color);
  anariCommitParameters(dev, mat);

  ANARISurface surf = anariNewSurface(dev);
  anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &g);
  anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
  anariCommitParameters(dev, surf);

  // Group holds ONE point light, positioned just in front of the quad's local
  // origin. Instance translations move each expanded copy to its own side.
  ANARILight light = anariNewLight(dev, "point");
  float lpos[3] = {0.f, 0.f, 0.4f};
  float intensity = 6.f;
  anariSetParameter(dev, light, "position", ANARI_FLOAT32_VEC3, lpos);
  anariSetParameter(dev, light, "intensity", ANARI_FLOAT32, &intensity);
  anariCommitParameters(dev, light);

  ANARIGroup group = anariNewGroup(dev);
  ANARIArray1D lightArray =
      anariNewArray1D(dev, &light, nullptr, nullptr, ANARI_LIGHT, 1);
  anariSetParameter(dev, group, "light", ANARI_ARRAY1D, &lightArray);
  anariCommitParameters(dev, group);

  std::vector<ANARIInstance> instances;
  for (float tx : instanceX) {
    // Column-major 4x4, translation in column 3.
    float xfm[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, tx, 0, 0, 1};
    ANARIInstance inst = anariNewInstance(dev, "transform");
    anariSetParameter(dev, inst, "group", ANARI_GROUP, &group);
    anariSetParameter(dev, inst, "transform", ANARI_FLOAT32_MAT4, xfm);
    anariCommitParameters(dev, inst);
    instances.push_back(inst);
  }

  ANARIWorld world = anariNewWorld(dev);
  ANARIArray1D surfArray =
      anariNewArray1D(dev, &surf, nullptr, nullptr, ANARI_SURFACE, 1);
  ANARIArray1D instArray = anariNewArray1D(dev,
      instances.data(),
      nullptr,
      nullptr,
      ANARI_INSTANCE,
      instances.size());
  anariSetParameter(dev, world, "surface", ANARI_ARRAY1D, &surfArray);
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
  double leftSum = 0, rightSum = 0;
  long leftN = 0, rightN = 0;
  if (pixels) {
    for (uint32_t y = 0; y < h; ++y) {
      for (uint32_t x = 0; x < w; ++x) {
        const uint8_t *p = (const uint8_t *)(pixels + y * w + x);
        const double lum = (p[0] + p[1] + p[2]) / 3.0;
        if (x < w / 3) {
          leftSum += lum;
          ++leftN;
        } else if (x >= 2 * w / 3) {
          rightSum += lum;
          ++rightN;
        }
      }
    }
  }
  anariUnmapFrame(dev, frame, "channel.color");
  leftAvg = leftN ? leftSum / leftN : 0;
  rightAvg = rightN ? rightSum / rightN : 0;

  anariRelease(dev, frame);
  anariRelease(dev, ren);
  anariRelease(dev, cam);
  anariRelease(dev, instArray);
  anariRelease(dev, surfArray);
  anariRelease(dev, world);
  for (ANARIInstance inst : instances)
    anariRelease(dev, inst);
  anariRelease(dev, lightArray);
  anariRelease(dev, group);
  anariRelease(dev, light);
  anariRelease(dev, surf);
  anariRelease(dev, mat);
  anariRelease(dev, g);
  anariRelease(dev, pos);
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

  // One instance at +X only: the single expanded light lights the right side;
  // the left stays comparatively dark (the control).
  double one_left = 0, one_right = 0;
  renderScene(dev, {0.9f}, one_left, one_right);

  // Two symmetric instances (+X and -X): expansion places a light on each side,
  // so BOTH the left and right regions light up.
  double two_left = 0, two_right = 0;
  renderScene(dev, {0.9f, -0.9f}, two_left, two_right);

  std::printf(
      "1 inst (+X): left=%.1f right=%.1f | 2 inst (+/-X): left=%.1f right=%.1f\n",
      one_left,
      one_right,
      two_left,
      two_right);

  // Control: a single instance lights its own side much brighter than the far
  // side — the light is localized (distance falloff), so an unexpanded scene
  // would leave the opposite side dark.
  check(one_right > one_left * 1.5,
      "single instance lights its own (right) side, far side stays dim");

  // Expansion proof: adding the -X instance lights up the left region that the
  // +X-only scene left dark. That brightening is a distinct second light sprim
  // from the second instance.
  check(two_left > one_left * 1.5,
      "second instance adds a light on the left (N instances -> N sprims)");

  // Both sides lit with two instances, and roughly balanced by symmetry.
  check(two_left > 20 && two_right > 20,
      "both left and right regions lit with two expanded lights");
  const double lo = std::min(two_left, two_right);
  const double hi = std::max(two_left, two_right);
  check(hi < lo * 2.0,
      "two symmetric instances light both sides comparably (expansion balanced)");

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
      g_errors);
  return g_errors == 0 ? 0 : 1;
}
