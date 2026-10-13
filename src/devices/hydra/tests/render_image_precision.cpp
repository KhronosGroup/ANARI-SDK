// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises image precision through the in-memory asset scheme (SPEC §6.3):
// decoded texels keep their numeric value instead of being clamped to [0,1]
// and quantised to 8 bits. A physicallyBased material with a black base and
// its `emissive` fed by the sampler, rendered with no lights into a
// FLOAT32_VEC4 frame, writes the sampled texel straight to the pixel, so the
// frame is a numeric readback of the texture:
//   - FLOAT32 texels above one and below zero survive (HDR / signed),
//   - a gradient finer than 1/255 stays monotone and distinct,
//   - sRGB 8-bit sources decode to linear without re-quantisation (dark codes
//     2, 5, 10 are distinct — all three round to 0 in 8-bit linear),
//   - UFIXED8 linear sources stay exact, UFIXED16 keeps sub-8-bit steps,
//   - image1D rides the same path,
//   - a channel-mixing outTransform may leave [0,1] (SPEC §6.4),
//   - live image unmap edits, a replacement of a different element type and
//     size, all propagate through the same frame,
//   - 8-bit sRGB sources stay 8-bit on the GPU (the `hydra.textureMemory`
//     frame property matches an 8-bit linear source of the same size), except
//     RA_SRGB, whose linear alpha sits in the green channel.

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
  if (severity <= ANARI_SEVERITY_WARNING)
    std::printf("[%s] %s\n",
        severity <= ANARI_SEVERITY_ERROR ? "ERR" : "WARN", message);
  if (severity <= ANARI_SEVERITY_ERROR)
    ++g_errors;
}

static void check(bool ok, const char *what)
{
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok)
    ++g_errors;
}

static bool near(float got, float want, float tol)
{
  return std::fabs(got - want) <= tol;
}

// *_SRGB sources decode with the piecewise IEC 61966-2-1 curve (helium's
// readAsAttributeValue, as GPU sRGB formats do); alpha stays linear.
static float srgbToLinear(int code)
{
  const float v = code / 255.f;
  return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

// The frame as floats; pixel (0,0) is ANARI's lower-left.
struct Img
{
  uint32_t w = 0, h = 0;
  std::vector<float> rgba;
  const float *at(int x, int y) const
  {
    return &rgba[(size_t(y) * w + x) * 4];
  }
};

static constexpr int kSize = 256;

// The quad spans [-0.9, 0.9] with st over the unit square, viewed from z=3 with
// fovy 0.6, so a texel of a W×H image covers a known pixel window; sample at
// the texel's centre. Image row j is ANARI v in [j/H, (j+1)/H] (v=0 at the
// bottom of the quad).
static const float *texelPixel(const Img &im, int W, int H, int i, int j)
{
  const float halfExtent = 3.f * std::tan(0.3f);
  const float u = (i + 0.5f) / W, v = (j + 0.5f) / H;
  const float nx = (u * 1.8f - 0.9f) / halfExtent;
  const float ny = (v * 1.8f - 0.9f) / halfExtent;
  const int px = int((nx + 1.f) * 0.5f * kSize);
  const int py = int((ny + 1.f) * 0.5f * kSize);
  return im.at(px, py);
}

// A quad whose emissive is the sampler; everything else is black/unlit.
struct Scene
{
  ANARIDevice dev{};
  ANARISampler sampler{};
  ANARIMaterial mat{};
  ANARIArray1D pos{}, st{};
  ANARIGeometry g{};
  ANARISurface surf{};
  ANARIArray1D surfArray{};
  ANARIWorld world{};
  ANARICamera cam{};
  ANARIRenderer ren{};
  ANARIFrame frame{};

  void build(ANARIDevice d, ANARISampler s)
  {
    dev = d;
    sampler = s;
    mat = anariNewMaterial(dev, "physicallyBased");
    const float black[3] = {0.f, 0.f, 0.f};
    const float one = 1.f, zero = 0.f;
    anariSetParameter(dev, mat, "baseColor", ANARI_FLOAT32_VEC3, black);
    anariSetParameter(dev, mat, "emissive", ANARI_SAMPLER, &sampler);
    anariSetParameter(dev, mat, "roughness", ANARI_FLOAT32, &one);
    anariSetParameter(dev, mat, "metallic", ANARI_FLOAT32, &zero);
    anariCommitParameters(dev, mat);

    static const float verts[12] = {-0.9f, -0.9f, 0.f, 0.9f, -0.9f, 0.f,
        0.9f, 0.9f, 0.f, -0.9f, 0.9f, 0.f};
    static const float uv[8] = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    pos = anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
    st = anariNewArray1D(dev, uv, nullptr, nullptr, ANARI_FLOAT32_VEC2, 4);
    g = anariNewGeometry(dev, "quad");
    anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
    anariSetParameter(dev, g, "vertex.attribute0", ANARI_ARRAY1D, &st);
    anariCommitParameters(dev, g);

    surf = anariNewSurface(dev);
    anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &g);
    anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
    anariCommitParameters(dev, surf);

    world = anariNewWorld(dev);
    surfArray = anariNewArray1D(dev, &surf, nullptr, nullptr, ANARI_SURFACE, 1);
    anariSetParameter(dev, world, "surface", ANARI_ARRAY1D, &surfArray);
    anariCommitParameters(dev, world);

    cam = anariNewCamera(dev, "perspective");
    const float eye[3] = {0.f, 0.f, 3.f};
    const float dir[3] = {0.f, 0.f, -1.f};
    const float up[3] = {0.f, 1.f, 0.f};
    const float fovy = 0.6f;
    anariSetParameter(dev, cam, "position", ANARI_FLOAT32_VEC3, eye);
    anariSetParameter(dev, cam, "direction", ANARI_FLOAT32_VEC3, dir);
    anariSetParameter(dev, cam, "up", ANARI_FLOAT32_VEC3, up);
    anariSetParameter(dev, cam, "fovy", ANARI_FLOAT32, &fovy);
    anariCommitParameters(dev, cam);

    ren = anariNewRenderer(dev, "default");
    const float background[4] = {0.f, 0.f, 0.f, 0.f};
    anariSetParameter(dev, ren, "background", ANARI_FLOAT32_VEC4, background);
    anariCommitParameters(dev, ren);

    frame = anariNewFrame(dev);
    const uint32_t size[2] = {kSize, kSize};
    const ANARIDataType colorFmt = ANARI_FLOAT32_VEC4;
    anariSetParameter(dev, frame, "size", ANARI_UINT32_VEC2, size);
    anariSetParameter(dev, frame, "channel.color", ANARI_DATA_TYPE, &colorFmt);
    anariSetParameter(dev, frame, "world", ANARI_WORLD, &world);
    anariSetParameter(dev, frame, "camera", ANARI_CAMERA, &cam);
    anariSetParameter(dev, frame, "renderer", ANARI_RENDERER, &ren);
    anariCommitParameters(dev, frame);
  }

  void render(Img &im)
  {
    anariRenderFrame(dev, frame);
    anariFrameReady(dev, frame, ANARI_WAIT);
    ANARIDataType pt = ANARI_UNKNOWN;
    const float *pixels = (const float *)anariMapFrame(
        dev, frame, "channel.color", &im.w, &im.h, &pt);
    im.rgba.clear();
    if (pixels && pt == ANARI_FLOAT32_VEC4)
      im.rgba.assign(pixels, pixels + size_t(im.w) * im.h * 4);
    anariUnmapFrame(dev, frame, "channel.color");
    if (im.rgba.empty()) {
      std::printf("  [FAIL] frame did not map as FLOAT32_VEC4\n");
      ++g_errors;
      im.rgba.assign(size_t(kSize) * kSize * 4, 0.f);
      im.w = im.h = kSize;
    }
  }

  void release()
  {
    for (ANARIObject o : {(ANARIObject)frame, (ANARIObject)ren,
             (ANARIObject)cam, (ANARIObject)surfArray, (ANARIObject)world,
             (ANARIObject)surf, (ANARIObject)g, (ANARIObject)st,
             (ANARIObject)pos, (ANARIObject)mat})
      anariRelease(dev, o);
  }
};

static ANARISampler newSampler(ANARIDevice dev, const char *subtype,
    ANARIObject image, ANARIDataType imageType)
{
  ANARISampler s = anariNewSampler(dev, subtype);
  anariSetParameter(dev, s, "image", imageType, &image);
  anariSetParameter(dev, s, "filter", ANARI_STRING, "nearest");
  anariCommitParameters(dev, s);
  return s;
}

// Storm's texture memory for a frame showing a kTexSize² image of `type`
// (every texel `texel`), via the `hydra.textureMemory` frame property; 0 when
// the property is unavailable. `centre` receives the sampled texel.
static constexpr int kTexSize = 64;

static uint64_t textureMemoryFor(ANARIDevice dev, ANARIDataType type,
    const std::vector<uint8_t> &texel, float centre[3]);

// Render `sampler` on the quad once and hand back the frame.
static void renderOnce(ANARIDevice dev, ANARISampler sampler, Img &im)
{
  Scene sc;
  sc.build(dev, sampler);
  sc.render(im);
  sc.release();
}

// Storm adds a small shading term to an unlit black-base surface (scene
// ambient on the dielectric specular lobe, ~0.01, varying slightly with the
// view angle but independent of the texture). Rendered once from a black texel
// and subtracted per pixel, so a sampled value is `pixel - baseline`.
static Img g_baseline;

static void sample(const Img &im, int W, int H, int i, int j, float out[3]);

static uint64_t textureMemoryFor(ANARIDevice dev, ANARIDataType type,
    const std::vector<uint8_t> &texel, float centre[3])
{
  std::vector<uint8_t> texels;
  for (int i = 0; i < kTexSize * kTexSize; ++i)
    texels.insert(texels.end(), texel.begin(), texel.end());
  ANARIArray2D image = anariNewArray2D(
      dev, texels.data(), nullptr, nullptr, type, kTexSize, kTexSize);
  ANARISampler s = newSampler(dev, "image2D", image, ANARI_ARRAY2D);
  Scene sc;
  sc.build(dev, s);
  Img im;
  sc.render(im);
  sample(im, kTexSize, kTexSize, kTexSize / 2, kTexSize / 2, centre);
  uint64_t bytes = 0;
  if (!anariGetProperty(dev, sc.frame, "hydra.textureMemory", ANARI_UINT64,
          &bytes, sizeof(bytes), ANARI_WAIT))
    bytes = 0;
  sc.release();
  anariRelease(dev, s);
  anariRelease(dev, image);
  return bytes;
}

static void sample(const Img &im, int W, int H, int i, int j, float out[3])
{
  const float *p = texelPixel(im, W, H, i, j);
  const float *b = texelPixel(g_baseline, W, H, i, j);
  for (int k = 0; k < 3; ++k)
    out[k] = p[k] - b[k];
}

// Storm's color buffer is half float, so allow the half rounding of the value
// itself (2^-11 relative) on top of the absolute tolerance.
static bool nearRel(float got, float want, float tol)
{
  return near(got, want, tol + 1e-3f * std::fabs(want));
}

static void checkTexel(const Img &im, int W, int H, int i, int j,
    const float want[3], float tol, const char *what)
{
  float p[3];
  sample(im, W, H, i, j, p);
  const bool ok = nearRel(p[0], want[0], tol) && nearRel(p[1], want[1], tol)
      && nearRel(p[2], want[2], tol);
  std::printf("    texel(%d,%d) = %.5f %.5f %.5f (want %.5f %.5f %.5f)\n", i,
      j, p[0], p[1], p[2], want[0], want[1], want[2]);
  check(ok, what);
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

  // --- Baseline: a black texel measures Storm's constant unlit shading term --
  {
    const float black[4] = {0.f, 0.f, 0.f, 1.f};
    ANARIArray2D image = anariNewArray2D(
        dev, black, nullptr, nullptr, ANARI_FLOAT32_VEC4, 1, 1);
    ANARISampler s = newSampler(dev, "image2D", image, ANARI_ARRAY2D);
    renderOnce(dev, s, g_baseline);
    const float *p = texelPixel(g_baseline, 1, 1, 0, 0);
    std::printf("black-texel baseline = %.5f %.5f %.5f\n", p[0], p[1], p[2]);
    check(p[0] < 0.05f && p[1] < 0.05f && p[2] < 0.05f,
        "black emissive texel renders near black (baseline)");
    anariRelease(dev, s);
    anariRelease(dev, image);
  }

  // --- FLOAT32 image2D: HDR and signed texels, live edits, replacement ------
  std::printf("FLOAT32_VEC4 image2D\n");
  {
    float texels[2 * 2 * 4] = {
        4.f, 0.f, 0.f, 1.f, /* (0,0) */ -0.5f, 0.25f, 0.f, 1.f, /* (1,0) */
        0.001f, 0.002f, 0.003f, 1.f, /* (0,1) */ 1.5f, 2.5f, 100.f, 1.f};
    ANARIArray2D image = anariNewArray2D(
        dev, texels, nullptr, nullptr, ANARI_FLOAT32_VEC4, 2, 2);
    ANARISampler s = newSampler(dev, "image2D", image, ANARI_ARRAY2D);
    Scene sc;
    sc.build(dev, s);
    Img im;
    sc.render(im);
    checkTexel(im, 2, 2, 0, 0, texels + 0, 2e-3f, "texel 4.0 survives (>1)");
    checkTexel(im, 2, 2, 1, 0, texels + 4, 2e-3f, "texel -0.5 survives (<0)");
    checkTexel(im, 2, 2, 0, 1, texels + 8, 1e-4f,
        "sub-1/255 texel 0.001/0.002/0.003 keeps its value");
    checkTexel(im, 2, 2, 1, 1, texels + 12, 0.1f, "texel 100 survives");

    // Live edit: an in-place unmap doubles every texel.
    {
      auto *p = static_cast<float *>(anariMapArray(dev, image));
      for (int k = 0; k < 16; ++k)
        if (k % 4 != 3)
          p[k] *= 2.f;
      anariUnmapArray(dev, image);
      for (int k = 0; k < 16; ++k)
        if (k % 4 != 3)
          texels[k] *= 2.f;
      sc.render(im);
      checkTexel(im, 2, 2, 0, 0, texels + 0, 4e-3f,
          "unmap edit: texel 8.0 reaches the frame");
      checkTexel(im, 2, 2, 1, 0, texels + 4, 2e-3f,
          "unmap edit: texel -1.0 reaches the frame");
    }

    // Replacement with a different element type and size (format conversion +
    // resize): a 3×1 UFIXED8 image, through the same sampler and frame.
    {
      const uint8_t small[3 * 4] = {
          37, 200, 0, 255, 255, 255, 255, 255, 0, 0, 51, 255};
      ANARIArray2D image8 = anariNewArray2D(
          dev, small, nullptr, nullptr, ANARI_UFIXED8_VEC4, 3, 1);
      anariSetParameter(dev, s, "image", ANARI_ARRAY2D, &image8);
      anariCommitParameters(dev, s);
      sc.render(im);
      const float t0[3] = {37 / 255.f, 200 / 255.f, 0.f};
      const float t1[3] = {1.f, 1.f, 1.f};
      const float t2[3] = {0.f, 0.f, 51 / 255.f};
      checkTexel(im, 3, 1, 0, 0, t0, 2e-4f,
          "replacement 3x1 UFIXED8: texel 0 exact (37/255, 200/255, 0)");
      checkTexel(im, 3, 1, 1, 0, t1, 2e-4f, "replacement: texel 1 white");
      checkTexel(im, 3, 1, 2, 0, t2, 2e-4f, "replacement: texel 2 (0,0,51/255)");
      anariRelease(dev, image8);

      // And back to a float image of yet another size.
      const float big[4] = {10.f, 0.5f, -2.f, 1.f};
      ANARIArray2D image1 = anariNewArray2D(
          dev, big, nullptr, nullptr, ANARI_FLOAT32_VEC4, 1, 1);
      anariSetParameter(dev, s, "image", ANARI_ARRAY2D, &image1);
      anariCommitParameters(dev, s);
      sc.render(im);
      checkTexel(im, 1, 1, 0, 0, big, 1e-2f,
          "replacement 1x1 FLOAT32: (10, 0.5, -2) reaches the frame");
      anariRelease(dev, image1);
    }
    sc.release();
    anariRelease(dev, s);
    anariRelease(dev, image);
  }

  // --- Fine gradient: 32 steps of 1/4096 -------------------------------------
  std::printf("fine gradient\n");
  {
    const int W = 32;
    std::vector<float> texels(size_t(W) * 4, 0.f);
    for (int i = 0; i < W; ++i) {
      texels[i * 4 + 0] = 0.1f + i / 4096.f;
      texels[i * 4 + 3] = 1.f;
    }
    ANARIArray2D image = anariNewArray2D(
        dev, texels.data(), nullptr, nullptr, ANARI_FLOAT32_VEC4, W, 1);
    ANARISampler s = newSampler(dev, "image2D", image, ANARI_ARRAY2D);
    Img im;
    renderOnce(dev, s, im);
    bool monotone = true, accurate = true;
    float prev = -1.f;
    for (int i = 0; i < W; ++i) {
      float px[3];
      sample(im, W, 1, i, 0, px);
      const float r = px[0];
      monotone &= r > prev;
      accurate &= near(r, texels[i * 4], 1e-4f);
      prev = r;
    }
    float first[3], last[3];
    sample(im, W, 1, 0, 0, first);
    sample(im, W, 1, W - 1, 0, last);
    std::printf("    first %.5f last %.5f\n", first[0], last[0]);
    check(monotone, "32 steps of 1/4096 are strictly increasing (not 8-bit)");
    check(accurate, "each gradient step is within 1e-4 of its texel");
    anariRelease(dev, s);
    anariRelease(dev, image);
  }

  // --- sRGB 8-bit source: dark codes stay distinct after linearisation --------
  // Codes 2, 5, 10 decode to 6.1e-4, 1.5e-3, 3.0e-3: in 8-bit linear storage
  // the first two would quantise to 0 and the third to 1/255.
  std::printf("UFIXED8_RGBA_SRGB image2D\n");
  {
    const int codes[4] = {2, 5, 10, 128};
    uint8_t texels[4 * 4];
    for (int i = 0; i < 4; ++i) {
      texels[i * 4 + 0] = texels[i * 4 + 1] = texels[i * 4 + 2] =
          uint8_t(codes[i]);
      texels[i * 4 + 3] = 255;
    }
    ANARIArray2D image = anariNewArray2D(
        dev, texels, nullptr, nullptr, ANARI_UFIXED8_RGBA_SRGB, 4, 1);
    ANARISampler s = newSampler(dev, "image2D", image, ANARI_ARRAY2D);
    Img im;
    renderOnce(dev, s, im);
    bool ok = true;
    float prev = -1.f;
    for (int i = 0; i < 4; ++i) {
      const float want = srgbToLinear(codes[i]);
      float px[3];
      sample(im, 4, 1, i, 0, px);
      const float got = px[0];
      std::printf("    code %3d -> %.6f (want %.6f)\n", codes[i], got, want);
      ok &= nearRel(got, want, 5e-5f) && got > prev + 5e-5f;
      prev = got;
    }
    check(ok, "sRGB codes 2, 5, 10, 128 decode to distinct linear values");
    anariRelease(dev, s);
    anariRelease(dev, image);
  }

  // --- sRGB 8-bit source: alpha stays linear ----------------------------------
  // A material texture's alpha drives opacity and alpha cutoff, so it must not
  // go through the sRGB curve. A channel-mixing outTransform copies alpha into
  // rgb (out.rgb = a) so the rendered value is the decoded alpha itself.
  std::printf("UFIXED8_RGBA_SRGB alpha\n");
  {
    const int codes[4] = {32, 64, 128, 200};
    uint8_t texels[4 * 4] = {};
    for (int i = 0; i < 4; ++i)
      texels[i * 4 + 3] = uint8_t(codes[i]);
    ANARIArray2D image = anariNewArray2D(
        dev, texels, nullptr, nullptr, ANARI_UFIXED8_RGBA_SRGB, 4, 1);
    ANARISampler s = newSampler(dev, "image2D", image, ANARI_ARRAY2D);
    // Column-major: the alpha (4th) column feeds r, g and b.
    const float alphaToRgb[16] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1};
    anariSetParameter(dev, s, "outTransform", ANARI_FLOAT32_MAT4, alphaToRgb);
    anariCommitParameters(dev, s);
    Img im;
    renderOnce(dev, s, im);
    bool ok = true;
    for (int i = 0; i < 4; ++i) {
      const float want = codes[i] / 255.f;
      float px[3];
      sample(im, 4, 1, i, 0, px);
      std::printf("    alpha %3d -> %.5f (want %.5f)\n", codes[i], px[0], want);
      ok &= nearRel(px[0], want, 1e-3f);
    }
    check(ok, "sRGB alpha codes 32, 64, 128, 200 decode linearly");
    anariRelease(dev, s);
    anariRelease(dev, image);
  }

  // --- sRGB 8-bit sources stay 8-bit on the GPU ------------------------------
  // Served as an sRGB-typed 8-bit texture, decoded by the GPU at sample time,
  // so they cost what an 8-bit linear source of the same size costs. RA_SRGB
  // keeps its linear alpha in green, which an sRGB texture would decode, so it
  // is linearised on the CPU into a wider format instead.
  std::printf("sRGB texture memory\n");
  {
    float c[3];
    const uint64_t linear =
        textureMemoryFor(dev, ANARI_UFIXED8_VEC4, {128, 128, 128, 255}, c);
    std::printf("    UFIXED8_VEC4 %llu bytes\n", (unsigned long long)linear);
    check(linear >= uint64_t(kTexSize) * kTexSize * 4,
        "hydra.textureMemory reports the 8-bit texture");

    const uint64_t rgba =
        textureMemoryFor(dev, ANARI_UFIXED8_RGBA_SRGB, {10, 64, 128, 255}, c);
    std::printf("    UFIXED8_RGBA_SRGB %llu bytes, texel %.5f %.5f %.5f\n",
        (unsigned long long)rgba, c[0], c[1], c[2]);
    check(rgba == linear, "RGBA_SRGB texture costs the same as 8-bit linear");
    check(nearRel(c[0], srgbToLinear(10), 5e-5f)
            && nearRel(c[1], srgbToLinear(64), 5e-5f)
            && nearRel(c[2], srgbToLinear(128), 5e-5f),
        "RGBA_SRGB texel decodes to linear");

    const uint64_t rgb =
        textureMemoryFor(dev, ANARI_UFIXED8_RGB_SRGB, {10, 64, 128}, c);
    std::printf("    UFIXED8_RGB_SRGB %llu bytes, texel %.5f %.5f %.5f\n",
        (unsigned long long)rgb, c[0], c[1], c[2]);
    check(rgb == linear, "RGB_SRGB texture costs the same as 8-bit linear");
    check(nearRel(c[0], srgbToLinear(10), 5e-5f)
            && nearRel(c[1], srgbToLinear(64), 5e-5f)
            && nearRel(c[2], srgbToLinear(128), 5e-5f),
        "RGB_SRGB texel decodes to linear");

    const uint64_t r = textureMemoryFor(dev, ANARI_UFIXED8_R_SRGB, {64}, c);
    std::printf("    UFIXED8_R_SRGB %llu bytes, texel %.5f %.5f %.5f\n",
        (unsigned long long)r, c[0], c[1], c[2]);
    check(r == linear, "R_SRGB texture costs the same as 8-bit linear");
    check(nearRel(c[0], srgbToLinear(64), 5e-5f) && near(c[1], 0.f, 5e-5f)
            && near(c[2], 0.f, 5e-5f),
        "R_SRGB texel decodes to (r, 0, 0)");

    const uint64_t ra =
        textureMemoryFor(dev, ANARI_UFIXED8_RA_SRGB, {64, 128}, c);
    std::printf("    UFIXED8_RA_SRGB %llu bytes, texel %.5f %.5f %.5f\n",
        (unsigned long long)ra, c[0], c[1], c[2]);
    check(ra > linear, "RA_SRGB is stored wider than 8 bits");
    check(nearRel(c[0], srgbToLinear(64), 5e-5f)
            && nearRel(c[1], 128 / 255.f, 1e-3f),
        "RA_SRGB texel decodes to (r, linear a)");
  }

  // --- UFIXED16: sub-8-bit steps survive --------------------------------------
  std::printf("UFIXED16_VEC4 image2D\n");
  {
    // Steps of 4/65535 = 6.1e-5: four half-float ulps at this magnitude (the
    // color buffer is half), yet all three codes quantise to 2/255 in 8-bit.
    const uint16_t texels[3 * 4] = {
        500, 0, 0, 65535, 504, 0, 0, 65535, 508, 0, 0, 65535};
    ANARIArray2D image = anariNewArray2D(
        dev, texels, nullptr, nullptr, ANARI_UFIXED16_VEC4, 3, 1);
    ANARISampler s = newSampler(dev, "image2D", image, ANARI_ARRAY2D);
    Img im;
    renderOnce(dev, s, im);
    bool ok = true;
    float prev = -1.f;
    for (int i = 0; i < 3; ++i) {
      const float want = (500 + 4 * i) / 65535.f;
      float px[3];
      sample(im, 3, 1, i, 0, px);
      const float got = px[0];
      std::printf("    %d/65535 -> %.7f (want %.7f)\n", 500 + 4 * i, got, want);
      ok &= near(got, want, 2e-5f) && got > prev;
      prev = got;
    }
    check(ok, "UFIXED16 codes 500, 504, 508 are distinct and accurate");
    anariRelease(dev, s);
    anariRelease(dev, image);
  }

  // --- image1D with HDR / signed FLOAT32_VEC3 texels --------------------------
  std::printf("FLOAT32_VEC3 image1D\n");
  {
    const float texels[3 * 3] = {3.f, 0.f, 0.f, 0.f, -1.f, 0.f, 0.f, 0.f, 7.f};
    ANARIArray1D image = anariNewArray1D(
        dev, texels, nullptr, nullptr, ANARI_FLOAT32_VEC3, 3);
    ANARISampler s = newSampler(dev, "image1D", image, ANARI_ARRAY1D);
    Img im;
    renderOnce(dev, s, im);
    checkTexel(im, 3, 1, 0, 0, texels + 0, 2e-3f, "image1D texel (3,0,0)");
    checkTexel(im, 3, 1, 1, 0, texels + 3, 2e-3f, "image1D texel (0,-1,0)");
    checkTexel(im, 3, 1, 2, 0, texels + 6, 5e-3f, "image1D texel (0,0,7)");
    anariRelease(dev, s);
    anariRelease(dev, image);
  }

  // --- channel-mixing outTransform leaving [0,1] ------------------------------
  std::printf("channel-mixing outTransform\n");
  {
    const uint8_t texel[4] = {255, 128, 0, 255};
    ANARIArray2D image = anariNewArray2D(
        dev, texel, nullptr, nullptr, ANARI_UFIXED8_VEC4, 1, 1);
    ANARISampler s = anariNewSampler(dev, "image2D");
    anariSetParameter(dev, s, "image", ANARI_ARRAY2D, &image);
    // out.r = 2r - g, out.g = g, out.b = b (column-major: m[col][row]).
    float m[16] = {};
    m[0 * 4 + 0] = 2.f;   // r <- 2r
    m[1 * 4 + 0] = -1.f;  // r <- -g
    m[1 * 4 + 1] = 1.f;
    m[2 * 4 + 2] = 1.f;
    m[3 * 4 + 3] = 1.f;
    anariSetParameter(dev, s, "outTransform", ANARI_FLOAT32_MAT4, m);
    anariCommitParameters(dev, s);
    Img im;
    renderOnce(dev, s, im);
    const float want[3] = {2.f - 128 / 255.f, 128 / 255.f, 0.f};
    checkTexel(im, 1, 1, 0, 0, want, 2e-3f,
        "mixed texel 2r - g = 1.498 is not clamped to 1");
    anariRelease(dev, s);
    anariRelease(dev, image);
  }

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", g_errors);
  return g_errors == 0 ? 0 : 1;
}
