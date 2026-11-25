// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// KHR_RENDERER_BACKGROUND_IMAGE: the `default` renderer's `background` given as
// an ARRAY2D is stretched over the whole frame and composited under the
// rendered color. The contract (matching VisRTX): frame pixel (x, y) samples
// the image at uv = ((x + 0.5) / W, (y + 0.5) / H) with bilinear filtering and
// clamp-to-edge wrapping, image row 0 on frame row 0 (ANARI's bottom row), and
// the result is the premultiplied "under" composite
//   rgb = C.rgb + (1 - C.a) * bg.rgb,   a = C.a + (1 - C.a) * bg.a.
//
// Everything renders into ONE non-square (96x48) frame from a 4x3 asymmetric
// image, so a transposed, flipped or squashed image fails the per-pixel check.
//   1. float image (HDR texels, varied alpha) under a float color channel:
//   every
//      miss pixel matches the reference sample; a bottom-left quad covers it.
//   2. sRGB 8-bit output encodes the composite.
//   3. element types: FLOAT32 (one channel) and UFIXED8_RGBA_SRGB (decoded).
//   4. an in-place map/unmap edit of the image array is followed.
//   5. a blended half-opaque quad composites over the image (Storm).
//   6. switching back to a constant FLOAT32_VEC4 background (alpha honored).
//   7. hdri domes (Storm): a visible dome is the backdrop and hides
//      the image; visible=false reveals the image again.

#include <anari/anari.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
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

struct RGBA
{
  float r, g, b, a;
};

static const uint32_t W = 96, H = 48; // non-square frame (aspect 2)
static const int IW = 4, IH = 3; // asymmetric image (aspect 4:3)

// Reference: bilinear, clamp-to-edge, texel centers at (i + 0.5) / n.
static RGBA sampleRef(
    const std::vector<RGBA> &img, int iw, int ih, float u, float v)
{
  const float fx = u * iw - 0.5f, fy = v * ih - 0.5f;
  const float x0f = std::floor(fx), y0f = std::floor(fy);
  const float tx = fx - x0f, ty = fy - y0f;
  auto at = [&](int x, int y) {
    x = std::clamp(x, 0, iw - 1);
    y = std::clamp(y, 0, ih - 1);
    return img[size_t(y) * iw + x];
  };
  const int x0 = int(x0f), y0 = int(y0f);
  const RGBA a = at(x0, y0), b = at(x0 + 1, y0), c = at(x0, y0 + 1),
             d = at(x0 + 1, y0 + 1);
  auto lerp2 = [&](float pa, float pb, float pc, float pd) {
    return (pa * (1 - tx) + pb * tx) * (1 - ty)
        + (pc * (1 - tx) + pd * tx) * ty;
  };
  return {lerp2(a.r, b.r, c.r, d.r),
      lerp2(a.g, b.g, c.g, d.g),
      lerp2(a.b, b.b, c.b, d.b),
      lerp2(a.a, b.a, c.a, d.a)};
}

static RGBA refPixel(
    const std::vector<RGBA> &img, int iw, int ih, uint32_t x, uint32_t y)
{
  return sampleRef(img, iw, ih, (x + 0.5f) / W, (y + 0.5f) / H);
}

static float srgbEncode(float v)
{
  v = std::clamp(v, 0.f, 1.f);
  return v <= 0.0031308f ? v * 12.92f
                         : 1.055f * std::pow(v, 1.f / 2.4f) - 0.055f;
}

static float srgbDecode(float v)
{
  return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

// The bottom-left quad (world x in [-1.8,-1.0], y in [-0.9,-0.3]) under the
// orthographic camera spanning x in [-2,2], y in [-1,1] covers pixels
// x in [4.8, 24], y in [2.4, 16.8] with row 0 at the bottom. Pixels at least
// two pixels outside that box are misses.
static bool inQuadMargin(uint32_t x, uint32_t y)
{
  return x >= 3 && x <= 26 && y >= 1 && y <= 19;
}
static const uint32_t kQuadX = 14, kQuadY = 9; // squarely inside the quad

struct Scene
{
  ANARIDevice dev{};
  ANARIArray1D pos{};
  ANARIGeometry geom{};
  ANARIMaterial mat{};
  ANARISurface surf{};
  ANARIArray1D surfArray{};
  ANARIWorld world{};
  ANARICamera cam{};
  ANARIRenderer ren{};
  ANARIFrame frame{};

  void init(ANARIDevice d)
  {
    dev = d;
    // Static: the array shares this memory (no deleter) for its lifetime.
    static const float verts[12] = {-1.8f,
        -0.9f,
        0.f,
        -1.0f,
        -0.9f,
        0.f,
        -1.0f,
        -0.3f,
        0.f,
        -1.8f,
        -0.3f,
        0.f};
    pos = anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
    geom = anariNewGeometry(dev, "quad");
    anariSetParameter(dev, geom, "vertex.position", ANARI_ARRAY1D, &pos);
    anariCommitParameters(dev, geom);

    mat = anariNewMaterial(dev, "matte");
    float white[3] = {1.f, 1.f, 1.f};
    anariSetParameter(dev, mat, "color", ANARI_FLOAT32_VEC3, white);
    anariCommitParameters(dev, mat);

    surf = anariNewSurface(dev);
    anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &geom);
    anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
    anariCommitParameters(dev, surf);

    world = anariNewWorld(dev);
    surfArray = anariNewArray1D(dev, &surf, nullptr, nullptr, ANARI_SURFACE, 1);
    anariSetParameter(dev, world, "surface", ANARI_ARRAY1D, &surfArray);
    anariCommitParameters(dev, world);

    cam = anariNewCamera(dev, "orthographic");
    float eye[3] = {0.f, 0.f, 3.f};
    float dir[3] = {0.f, 0.f, -1.f};
    float up[3] = {0.f, 1.f, 0.f};
    float height = 2.f, aspect = 2.f;
    anariSetParameter(dev, cam, "position", ANARI_FLOAT32_VEC3, eye);
    anariSetParameter(dev, cam, "direction", ANARI_FLOAT32_VEC3, dir);
    anariSetParameter(dev, cam, "up", ANARI_FLOAT32_VEC3, up);
    anariSetParameter(dev, cam, "height", ANARI_FLOAT32, &height);
    anariSetParameter(dev, cam, "aspect", ANARI_FLOAT32, &aspect);
    anariCommitParameters(dev, cam);

    ren = anariNewRenderer(dev, "default");
    anariCommitParameters(dev, ren);

    frame = anariNewFrame(dev);
    uint32_t size[2] = {W, H};
    anariSetParameter(dev, frame, "size", ANARI_UINT32_VEC2, size);
    anariSetParameter(dev, frame, "world", ANARI_WORLD, &world);
    anariSetParameter(dev, frame, "camera", ANARI_CAMERA, &cam);
    anariSetParameter(dev, frame, "renderer", ANARI_RENDERER, &ren);
    setColorType(ANARI_FLOAT32_VEC4);
  }

  void setColorType(ANARIDataType t)
  {
    anariSetParameter(dev, frame, "channel.color", ANARI_DATA_TYPE, &t);
    anariCommitParameters(dev, frame);
  }

  void setBackgroundArray(ANARIArray2D a)
  {
    anariSetParameter(dev, ren, "background", ANARI_ARRAY2D, &a);
    anariCommitParameters(dev, ren);
  }

  // Render and read the color channel back as float RGBA (8-bit channels are
  // returned in their encoded 0..1 value).
  std::vector<RGBA> render()
  {
    anariRenderFrame(dev, frame);
    anariFrameReady(dev, frame, ANARI_WAIT);
    uint32_t w = 0, h = 0;
    ANARIDataType pt = ANARI_UNKNOWN;
    const void *p = anariMapFrame(dev, frame, "channel.color", &w, &h, &pt);
    std::vector<RGBA> out;
    if (p && w == W && h == H) {
      out.resize(size_t(w) * h);
      for (size_t i = 0; i < out.size(); ++i) {
        if (pt == ANARI_FLOAT32_VEC4) {
          std::memcpy(&out[i], (const float *)p + i * 4, 16);
        } else {
          const uint8_t *b = (const uint8_t *)p + i * 4;
          out[i] = {b[0] / 255.f, b[1] / 255.f, b[2] / 255.f, b[3] / 255.f};
        }
      }
    }
    anariUnmapFrame(dev, frame, "channel.color");
    return out;
  }

  void release()
  {
    anariRelease(dev, frame);
    anariRelease(dev, ren);
    anariRelease(dev, cam);
    anariRelease(dev, surfArray);
    anariRelease(dev, world);
    anariRelease(dev, surf);
    anariRelease(dev, mat);
    anariRelease(dev, geom);
    anariRelease(dev, pos);
  }
};

// Compare every miss pixel of `px` against `expected(x, y)`; report the max
// abs error.
template <typename F>
static bool missesMatch(
    const std::vector<RGBA> &px, F expected, float tol, const char *label)
{
  if (px.size() != size_t(W) * H) {
    std::printf("    %s: no pixels\n", label);
    return false;
  }
  float maxErr = 0.f;
  uint32_t ex = 0, ey = 0;
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      if (inQuadMargin(x, y))
        continue;
      const RGBA e = expected(x, y);
      const RGBA g = px[size_t(y) * W + x];
      const float err = std::max({std::fabs(e.r - g.r),
          std::fabs(e.g - g.g),
          std::fabs(e.b - g.b),
          std::fabs(e.a - g.a)});
      if (err > maxErr) {
        maxErr = err;
        ex = x;
        ey = y;
      }
    }
  const RGBA e = expected(ex, ey), g = px[size_t(ey) * W + ex];
  std::printf(
      "    %s: max err %.5f at (%u,%u) exp %.3f %.3f %.3f %.3f got "
      "%.3f %.3f %.3f %.3f\n",
      label,
      maxErr,
      ex,
      ey,
      e.r,
      e.g,
      e.b,
      e.a,
      g.r,
      g.g,
      g.b,
      g.a);
  return maxErr <= tol;
}

static ANARIArray2D newImage(
    ANARIDevice dev, ANARIDataType t, const void *data, int w, int h)
{
  return anariNewArray2D(dev, data, nullptr, nullptr, t, w, h);
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

  bool hasImageExt = false, hasHdri = false;
  if (const char *const *ext = (const char *const *)anariGetObjectInfo(
          dev, ANARI_DEVICE, "default", "extension", ANARI_STRING_LIST)) {
    for (; *ext; ++ext) {
      hasImageExt |=
          std::strcmp(*ext, "ANARI_KHR_RENDERER_BACKGROUND_IMAGE") == 0;
      hasHdri |= std::strcmp(*ext, "ANARI_KHR_LIGHT_HDRI") == 0;
    }
  }
  check(hasImageExt, "device advertises KHR_RENDERER_BACKGROUND_IMAGE");

  Scene s;
  s.init(dev);

  // The asymmetric image: distinct texels, HDR values (> 1) and varied alpha.
  std::vector<RGBA> img(IW * IH);
  for (int y = 0; y < IH; ++y)
    for (int x = 0; x < IW; ++x)
      img[size_t(y) * IW + x] = {0.1f + 0.6f * x,
          0.2f + 0.4f * y,
          (x + y) % 2 ? 0.9f : 0.05f,
          0.25f + 0.25f * float((x + 2 * y) % 4)};
  ANARIArray2D imgArray = newImage(dev, ANARI_FLOAT32_VEC4, img.data(), IW, IH);

  // --- 1. float image under a float color channel --------------------------
  s.setBackgroundArray(imgArray);
  std::vector<RGBA> px = s.render();
  check(missesMatch(
            px,
            [&](uint32_t x, uint32_t y) { return refPixel(img, IW, IH, x, y); },
            2e-3f,
            "float image"),
      "case1: every miss samples the stretched image (orientation, bilinear, "
      "HDR, alpha)");
  if (!px.empty()) {
    const RGBA q = px[kQuadY * W + kQuadX];
    const RGBA mirrored = px[(H - 1 - kQuadY) * W + kQuadX];
    std::printf("    quad pixel %.3f %.3f %.3f %.3f\n", q.r, q.g, q.b, q.a);
    check(std::fabs(q.a - 1.f) < 1e-3f && q.r > 0.05f
            && std::fabs(q.r - q.g) < 0.05f && std::fabs(q.g - q.b) < 0.05f,
        "case1: the bottom-left quad covers the image (opaque, grey-white)");
    const RGBA mref = refPixel(img, IW, IH, kQuadX, H - 1 - kQuadY);
    check(std::fabs(mirrored.r - mref.r) < 2e-3f,
        "case1: the mirrored top-left pixel is background (row 0 is bottom)");
  }

  // --- 2. sRGB 8-bit output --------------------------------------------------
  s.setColorType(ANARI_UFIXED8_RGBA_SRGB);
  px = s.render();
  check(missesMatch(
            px,
            [&](uint32_t x, uint32_t y) {
              RGBA r = refPixel(img, IW, IH, x, y);
              return RGBA{srgbEncode(r.r),
                  srgbEncode(r.g),
                  srgbEncode(r.b),
                  std::clamp(r.a, 0.f, 1.f)};
            },
            2.5f / 255.f,
            "srgb8 output"),
      "case2: an sRGB color channel encodes the composited image");
  s.setColorType(ANARI_FLOAT32_VEC4);

  // --- 3. element types
  // --------------------------------------------------------
  {
    std::vector<float> mono(IW * IH);
    std::vector<RGBA> monoRef(IW * IH);
    for (int i = 0; i < IW * IH; ++i) {
      mono[i] = 0.1f * i;
      monoRef[i] = {mono[i], 0.f, 0.f, 1.f};
    }
    ANARIArray2D a = newImage(dev, ANARI_FLOAT32, mono.data(), IW, IH);
    s.setBackgroundArray(a);
    px = s.render();
    check(missesMatch(
              px,
              [&](uint32_t x, uint32_t y) {
                return refPixel(monoRef, IW, IH, x, y);
              },
              2e-3f,
              "FLOAT32 image"),
        "case3: a one-channel float image reads as (r, 0, 0, 1)");
    anariRelease(dev, a);

    std::vector<uint8_t> u8(IW * IH * 4);
    std::vector<RGBA> u8Ref(IW * IH);
    for (int i = 0; i < IW * IH; ++i) {
      u8[i * 4 + 0] = uint8_t(20 * i);
      u8[i * 4 + 1] = uint8_t(255 - 20 * i);
      u8[i * 4 + 2] = uint8_t(128);
      u8[i * 4 + 3] = uint8_t(255 - 10 * i);
      u8Ref[i] = {srgbDecode(u8[i * 4] / 255.f),
          srgbDecode(u8[i * 4 + 1] / 255.f),
          srgbDecode(u8[i * 4 + 2] / 255.f),
          u8[i * 4 + 3] / 255.f};
    }
    a = newImage(dev, ANARI_UFIXED8_RGBA_SRGB, u8.data(), IW, IH);
    s.setBackgroundArray(a);
    px = s.render();
    check(missesMatch(
              px,
              [&](uint32_t x, uint32_t y) {
                return refPixel(u8Ref, IW, IH, x, y);
              },
              2e-3f,
              "UFIXED8_RGBA_SRGB image"),
        "case3: an sRGB 8-bit image is linearised (alpha stays linear)");
    anariRelease(dev, a);
  }

  // --- 4. in-place image edit
  // -------------------------------------------------
  s.setBackgroundArray(imgArray);
  {
    RGBA *p = (RGBA *)anariMapArray(dev, imgArray);
    for (int i = 0; i < IW * IH; ++i) {
      img[i] = {img[i].b, img[i].r, img[i].g, 1.f - 0.5f * img[i].a};
      p[i] = img[i];
    }
    anariUnmapArray(dev, imgArray);
  }
  px = s.render();
  check(missesMatch(
            px,
            [&](uint32_t x, uint32_t y) { return refPixel(img, IW, IH, x, y); },
            2e-3f,
            "edited image"),
      "case4: a map/unmap edit of the image array is followed");

  // --- 5. blended half-opaque quad over the image
  // ------------------------------- HdEmbree has no material networks, so
  // material opacity never reaches it.
  if (std::strcmp(subtype, "storm") == 0) {
    float half = 0.5f;
    anariSetParameter(dev, s.mat, "opacity", ANARI_FLOAT32, &half);
    anariSetParameter(dev, s.mat, "alphaMode", ANARI_STRING, "blend");
    anariCommitParameters(dev, s.mat);
    px = s.render();
    if (!px.empty()) {
      const RGBA q = px[kQuadY * W + kQuadX];
      const RGBA b = refPixel(img, IW, IH, kQuadX, kQuadY);
      const float expA = 0.5f + 0.5f * b.a;
      std::printf(
          "    blended quad %.3f %.3f %.3f %.3f  bg %.3f %.3f %.3f %.3f\n",
          q.r,
          q.g,
          q.b,
          q.a,
          b.r,
          b.g,
          b.b,
          b.a);
      check(std::fabs(q.a - expA) < 0.02f,
          "case5: a half-opaque quad composites alpha over the image");
      // The quad is neutral grey-white; the image texel is not, so the
      // background's hue must show through in the rendered color.
      check(std::fabs(q.b - q.g) > 0.1f * std::fabs(b.b - b.g),
          "case5: the image shows through the half-opaque quad");
    }
    float one = 1.f;
    anariSetParameter(dev, s.mat, "opacity", ANARI_FLOAT32, &one);
    anariSetParameter(dev, s.mat, "alphaMode", ANARI_STRING, "opaque");
    anariCommitParameters(dev, s.mat);
  }

  // --- 6. back to a constant background
  // -----------------------------------------
  {
    const float bg[4] = {0.2f, 0.4f, 0.6f, 0.5f};
    anariSetParameter(dev, s.ren, "background", ANARI_FLOAT32_VEC4, bg);
    anariCommitParameters(dev, s.ren);
    px = s.render();
    check(missesMatch(
              px,
              [&](uint32_t, uint32_t) {
                return RGBA{bg[0], bg[1], bg[2], bg[3]};
              },
              2e-3f,
              "constant"),
        "case6: switching back to a constant background replaces the image");
  }

  // --- 7. hdri dome interaction
  // --------------------------------------------------- HdEmbree advertises the
  // dome sprim but cannot load the bridge's anari:// environment texture, so
  // its hdri is never a backdrop; Storm only.
  if (hasHdri && std::strcmp(subtype, "storm") == 0) {
    s.setBackgroundArray(imgArray);
    std::vector<float> env(8 * 8 * 3, 0.f);
    for (int i = 0; i < 64; ++i)
      env[i * 3] = 1.f; // solid red environment
    ANARIArray2D radiance = anariNewArray2D(
        dev, env.data(), nullptr, nullptr, ANARI_FLOAT32_VEC3, 8, 8);
    ANARILight light = anariNewLight(dev, "hdri");
    anariSetParameter(dev, light, "radiance", ANARI_ARRAY2D, &radiance);
    anariCommitParameters(dev, light);
    ANARIArray1D lights =
        anariNewArray1D(dev, &light, nullptr, nullptr, ANARI_LIGHT, 1);
    anariSetParameter(dev, s.world, "light", ANARI_ARRAY1D, &lights);
    anariCommitParameters(dev, s.world);

    px = s.render();
    if (!px.empty()) {
      const RGBA c = px[(H - 2) * W + (W - 2)];
      std::printf(
          "    visible dome corner %.3f %.3f %.3f %.3f\n", c.r, c.g, c.b, c.a);
      check(c.r > 0.3f && c.g < 0.1f && c.b < 0.1f
              && std::fabs(c.a - 1.f) < 1e-3f,
          "case7: a visible hdri is the backdrop and hides the image");
    }

    uint32_t vis = 0;
    anariSetParameter(dev, light, "visible", ANARI_BOOL, &vis);
    anariCommitParameters(dev, light);
    px = s.render();
    check(
        missesMatch(
            px,
            [&](uint32_t x, uint32_t y) { return refPixel(img, IW, IH, x, y); },
            2e-3f,
            "invisible dome"),
        "case7: hdri visible=false reveals the image");

    anariUnsetParameter(dev, s.world, "light");
    anariCommitParameters(dev, s.world);
    anariRelease(dev, lights);
    anariRelease(dev, light);
    anariRelease(dev, radiance);
  } else {
    std::printf("  [SKIP] case7: delegate has no hdri light\n");
  }

  anariRelease(dev, imgArray);
  s.release();
  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
      g_errors);
  return g_errors == 0 ? 0 : 1;
}
