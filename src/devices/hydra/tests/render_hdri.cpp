// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises the hdri dome light's environment image riding the in-memory asset
// scheme. An hdri light's `radiance` is set to a solid-color equirectangular
// image; the device decodes it into the AnariImage registry under the light's
// `anari://light/lN.anari` path and authors the dome light's textureFile at it,
// so Storm loads it through Glf->Hio->AnariHioImage (the same plugin the
// samplers use) instead of the constant-color fallback.
//
// A sharp, non-noisy check: a white matte quad lit only by the dome tints
// toward the dome image's color. Render it twice (red dome, then green dome)
// and assert relative dominance — the red-dome render is red-dominant, the
// green-dome render green-dominant — rather than a brittle absolute threshold.

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

// Render a white matte quad lit only by an hdri dome whose environment image is
// a solid (r,g,b); return the average color over covered pixels (0..255 through
// the default sRGB 8-bit frame, or linear float values with `floatFrame`).
// With `srgb`, the image is UFIXED8_RGB_SRGB of those codes instead of `rgb`.
// `lightColor` sets the light's `color`. With `editScale` > 0, the dome is
// camera-invisible and the frame is rendered once, then the same light's
// `scale` is changed in place to `editScale` and the frame re-rendered; the
// second render is measured.
static void renderDomeTint(ANARIDevice dev,
    const float rgb[3],
    double out[3],
    bool floatFrame = false,
    float scale = 1.f,
    const uint8_t *srgb = nullptr,
    const float *lightColor = nullptr,
    float editScale = 0.f)
{
  out[0] = out[1] = out[2] = 0.0;

  // A solid 8x8 equirectangular environment of the given color.
  const int W = 8, H = 8;
  std::vector<float> env(size_t(W) * H * 3);
  std::vector<uint8_t> envSrgb(size_t(W) * H * 3);
  for (int i = 0; i < W * H; ++i) {
    for (int k = 0; k < 3; ++k) {
      env[i * 3 + k] = rgb ? rgb[k] : 0.f;
      envSrgb[i * 3 + k] = srgb ? srgb[k] : 0;
    }
  }
  ANARIArray2D radiance = srgb
      ? anariNewArray2D(
            dev, envSrgb.data(), nullptr, nullptr, ANARI_UFIXED8_RGB_SRGB, W, H)
      : anariNewArray2D(
            dev, env.data(), nullptr, nullptr, ANARI_FLOAT32_VEC3, W, H);

  ANARILight light = anariNewLight(dev, "hdri");
  anariSetParameter(dev, light, "radiance", ANARI_ARRAY2D, &radiance);
  anariSetParameter(dev, light, "scale", ANARI_FLOAT32, &scale);
  if (lightColor)
    anariSetParameter(dev, light, "color", ANARI_FLOAT32_VEC3, lightColor);
  // The edit case measures the lit surface alone: HdEmbree re-adds a re-synced
  // dome to its camera-visible list, so after an edit a visible dome's backdrop
  // sums two copies (a delegate bug; the lighting itself is right).
  if (editScale > 0.f) {
    const bool invisible = false;
    anariSetParameter(dev, light, "visible", ANARI_BOOL, &invisible);
  }
  anariCommitParameters(dev, light);

  // A white matte quad facing the camera; its color comes only from the dome.
  float verts[12] = {
      -0.8f, -0.8f, 0.f, 0.8f, -0.8f, 0.f, 0.8f, 0.8f, 0.f, -0.8f, 0.8f, 0.f};
  ANARIArray1D pos =
      anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  ANARIGeometry g = anariNewGeometry(dev, "quad");
  anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
  anariCommitParameters(dev, g);

  ANARIMaterial mat = anariNewMaterial(dev, "matte");
  float white[3] = {1.f, 1.f, 1.f};
  anariSetParameter(dev, mat, "color", ANARI_FLOAT32_VEC3, white);
  anariCommitParameters(dev, mat);

  ANARISurface surf = anariNewSurface(dev);
  anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &g);
  anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
  anariCommitParameters(dev, surf);

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
  ANARIDataType colorFmt =
      floatFrame ? ANARI_FLOAT32_VEC4 : ANARI_UFIXED8_RGBA_SRGB;
  anariSetParameter(dev, frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(dev, frame, "channel.color", ANARI_DATA_TYPE, &colorFmt);
  anariSetParameter(dev, frame, "world", ANARI_WORLD, &world);
  anariSetParameter(dev, frame, "camera", ANARI_CAMERA, &cam);
  anariSetParameter(dev, frame, "renderer", ANARI_RENDERER, &ren);
  anariCommitParameters(dev, frame);

  anariRenderFrame(dev, frame);
  anariFrameReady(dev, frame, ANARI_WAIT);
  if (editScale > 0.f) {
    anariSetParameter(dev, light, "scale", ANARI_FLOAT32, &editScale);
    anariCommitParameters(dev, light);
    anariRenderFrame(dev, frame);
    anariFrameReady(dev, frame, ANARI_WAIT);
  }

  uint32_t w = 0, h = 0;
  ANARIDataType pt = ANARI_UNKNOWN;
  const void *pixels = anariMapFrame(dev, frame, "channel.color", &w, &h, &pt);
  double s[3] = {0, 0, 0};
  long n = 0;
  if (pixels) {
    for (uint32_t i = 0; i < w * h; ++i) {
      double p[3];
      if (pt == ANARI_FLOAT32_VEC4) {
        const float *f = static_cast<const float *>(pixels) + size_t(i) * 4;
        p[0] = f[0];
        p[1] = f[1];
        p[2] = f[2];
      } else {
        const uint8_t *b = static_cast<const uint8_t *>(pixels) + size_t(i) * 4;
        p[0] = b[0];
        p[1] = b[1];
        p[2] = b[2];
      }
      // Skip the dark clear color; count lit surface pixels only.
      const double dark = floatFrame ? 0.01 : 10;
      if (p[0] > dark || p[1] > dark || p[2] > dark) {
        s[0] += p[0];
        s[1] += p[1];
        s[2] += p[2];
        ++n;
      }
    }
  }
  anariUnmapFrame(dev, frame, "channel.color");
  if (n) {
    out[0] = s[0] / n;
    out[1] = s[1] / n;
    out[2] = s[2] / n;
  }

  anariRelease(dev, frame);
  anariRelease(dev, ren);
  anariRelease(dev, cam);
  anariRelease(dev, lightArray);
  anariRelease(dev, surfArray);
  anariRelease(dev, world);
  anariRelease(dev, surf);
  anariRelease(dev, mat);
  anariRelease(dev, g);
  anariRelease(dev, pos);
  anariRelease(dev, light);
  anariRelease(dev, radiance);
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

  const float red[3] = {1.f, 0.f, 0.f};
  const float green[3] = {0.f, 1.f, 0.f};
  double rTint[3], gTint[3];
  renderDomeTint(dev, red, rTint);
  renderDomeTint(dev, green, gTint);

  std::printf(
      "red-dome tint   = %.1f %.1f %.1f\n", rTint[0], rTint[1], rTint[2]);
  std::printf(
      "green-dome tint = %.1f %.1f %.1f\n", gTint[0], gTint[1], gTint[2]);

  check(rTint[0] + rTint[1] + rTint[2] > 5.0
          && gTint[0] + gTint[1] + gTint[2] > 5.0,
      "dome-lit surface is visible under both domes");

  // The dome image reaches the shading, not a constant color: the red-dome
  // render is red-dominant and the green-dome render green-dominant. Relative
  // dominance, so no brittle absolute threshold.
  check(rTint[0] > rTint[1] && rTint[0] > rTint[2],
      "red dome tints the surface red (image sampled, not constant color)");
  check(gTint[1] > gTint[0] && gTint[1] > gTint[2],
      "green dome tints the surface green");
  check(rTint[0] > gTint[0] && gTint[1] > rTint[1],
      "swapping the dome image swaps the surface tint");

  // HDR radiance: a float dome image keeps its precision, so a 4x dome lights
  // the surface 4x as brightly as a 1x dome.
  const float one[3] = {1.f, 1.f, 1.f};
  const float four[3] = {4.f, 4.f, 4.f};
  double lit1[3], lit4[3];
  renderDomeTint(dev, one, lit1, true);
  renderDomeTint(dev, four, lit4, true);
  std::printf("dome 1.0 lit = %.4f %.4f %.4f\n", lit1[0], lit1[1], lit1[2]);
  std::printf("dome 4.0 lit = %.4f %.4f %.4f\n", lit4[0], lit4[1], lit4[2]);
  const double ratio = lit1[1] > 0 ? lit4[1] / lit1[1] : 0;
  std::printf("ratio = %.3f\n", ratio);
  check(lit1[1] > 0.5 && lit1[1] < 1.5,
      "unit dome lights the white surface ~1.0 (float frame, deterministic)");
  check(ratio > 3.9 && ratio < 4.1,
      "HDR dome radiance 4.0 lights the surface 4x a 1.0 dome (not clamped)");

  // The light's `scale` and `color` multiply the radiance image on every
  // delegate (HdEmbree's dome ignores the dome's intensity and color, so the
  // device folds them into the image it serves), and an in-place `scale` edit
  // of a live light is followed.
  {
    const float tint[3] = {1.f, 0.5f, 0.25f};
    double tinted[3], edited[3];
    renderDomeTint(dev, one, tinted, true, 2.f, nullptr, tint);
    renderDomeTint(dev, one, edited, true, 1.f, nullptr, nullptr, 3.f);
    std::printf("dome x2 color (1,.5,.25) lit = %.4f %.4f %.4f\n",
        tinted[0],
        tinted[1],
        tinted[2]);
    std::printf("dome scale 1 -> 3 in place lit = %.4f %.4f %.4f\n",
        edited[0],
        edited[1],
        edited[2]);
    bool scaled = true;
    for (int k = 0; k < 3; ++k) {
      const double want = 2.0 * tint[k] * lit1[k];
      scaled &= tinted[k] > 0.95 * want && tinted[k] < 1.05 * want;
    }
    check(scaled, "hdri scale and color multiply the radiance image");
    check(edited[1] > 2.85 * lit1[1] && edited[1] < 3.15 * lit1[1],
        "an in-place scale edit relights the surface");
  }

  // sRGB dome image: served as an sRGB-typed 8-bit texture the GPU decodes, so
  // a dark code keeps its exact linear value instead of being re-quantised to
  // 8-bit linear (code 10 is 0.003035; 8-bit linear storage would round it to
  // 1/255 = 0.00392, ~29% bright). Compared at scale 100 against the unit float
  // dome, which lights the surface ~1.0.
  {
    const uint8_t code10[3] = {10, 10, 10};
    double dark[3];
    renderDomeTint(dev, nullptr, dark, true, 100.f, code10);
    const double want = 100.0 * (10.0 / 255.0 / 12.92) * lit1[1];
    std::printf("sRGB code-10 dome x100 lit = %.4f %.4f %.4f (want %.4f)\n",
        dark[0],
        dark[1],
        dark[2],
        want);
    check(dark[1] > 0.95 * want && dark[1] < 1.05 * want,
        "sRGB dome code 10 lights with its exact linear value");
    check(dark[0] > 0.95 * want && dark[2] > 0.95 * want
            && dark[0] < 1.05 * want && dark[2] < 1.05 * want,
        "sRGB dome decodes every colour channel");
  }

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
      g_errors);
  return g_errors == 0 ? 0 : 1;
}
