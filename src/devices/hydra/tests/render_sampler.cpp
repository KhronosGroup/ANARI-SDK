// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises the samplers/textures path (SPEC §6.2-6.4). A material `baseColor`
// is set to an image2D sampler holding a 2x2 checkerboard; the device decodes
// the image into the in-memory asset registry, authors a UsdUVTexture (+
// UsdPrimvarReader_float2 feeding `st` from vertex.attribute0) into the
// material network, and Storm samples it onto the quad. This asserts the
// observable, spec-guaranteed result: each quadrant of the quad renders with
// its checkerboard color — proving the whole in-memory asset scheme end to end.

#include <anari/anari.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static int g_errors = 0;
static bool g_faceVarying = false;

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

struct Img
{
  uint32_t w = 0, h = 0;
  std::vector<uint8_t> rgba;
  const uint8_t *at(int x, int y) const
  {
    return &rgba[(size_t(y) * w + x) * 4];
  }
};

// Average color over an axis-aligned window, skipping the dark clear color.
static void windowAvg(const Img &im, int cx, int cy, int r, double out[3])
{
  double s[3] = {0, 0, 0};
  long n = 0;
  for (int y = cy - r; y <= cy + r; ++y)
    for (int x = cx - r; x <= cx + r; ++x) {
      if (x < 0 || y < 0 || x >= int(im.w) || y >= int(im.h))
        continue;
      const uint8_t *p = im.at(x, y);
      if (p[0] > 20 || p[1] > 20 || p[2] > 20) {
        s[0] += p[0];
        s[1] += p[1];
        s[2] += p[2];
        ++n;
      }
    }
  for (int k = 0; k < 3; ++k)
    out[k] = n ? s[k] / n : 0;
}

// Count "blended" pixels along a horizontal scanline crossing the u=0.5 texel
// boundary in the bottom (v=0) row: red (u<0.5) -> green (u>0.5). A covered
// pixel is red-dominant (r >> g), green-dominant (g >> r), or blended
// (neither). nearest sampling snaps at the boundary (few blended px); linear
// interpolates across the whole texel-center span (~half the quad, many blended
// px).
static int countBlended(const Img &im, int y, int x0, int x1)
{
  int blended = 0;
  for (int x = x0; x <= x1; ++x) {
    const uint8_t *p = im.at(x, y);
    if (p[0] <= 20 && p[1] <= 20 && p[2] <= 20)
      continue; // uncovered / clear color
    const bool redDom = p[0] > p[1] + 40;
    const bool greenDom = p[1] > p[0] + 40;
    if (!redDom && !greenDom)
      ++blended;
  }
  return blended;
}

// Full scene state, so it can be rebuilt/released per filter mode.
struct Scene
{
  ANARIArray2D image;
  ANARISampler sampler;
  ANARIMaterial mat;
  ANARIArray1D pos, attr0;
  ANARIGeometry g;
  ANARISurface surf;
  ANARIArray1D surfArray;
  ANARIWorld world;
  ANARICamera cam;
  ANARIRenderer ren;
  ANARIFrame frame;
};

// Build + render the 2x2 checkerboard quad with the given sampler `filter`
// (nullptr = leave the param unset). Returns the mapped frame in `im`.
static void renderChecker(ANARIDevice dev,
    const char *filter,
    Img &im,
    Img *mutated = nullptr,
    bool flipV = false,
    const char *alphaMode = nullptr,
    int opacityChannel = -1,
    bool updates = false)
{
  Scene sc;

  // A 2x2 checkerboard image2D: (u,v) row 0 (v=0) = red, green; row 1 (v=1) =
  // blue, white. UFIXED8_VEC4 (linear), row-major, row 0 = v=0.
  static uint8_t checker[2 * 2 * 4] = {255,
      0,
      0,
      255,
      /* (0,0) R */ 0,
      255,
      0,
      255, /* (1,0) G */
      0,
      0,
      255,
      255,
      /* (0,1) B */ 255,
      255,
      255,
      255 /* (1,1) W */};
  sc.image =
      anariNewArray2D(dev, checker, nullptr, nullptr, ANARI_UFIXED8_VEC4, 2, 2);

  sc.sampler = anariNewSampler(dev, "image2D");
  anariSetParameter(dev, sc.sampler, "image", ANARI_ARRAY2D, &sc.image);
  const char *wrap = "clampToEdge";
  anariSetParameter(dev, sc.sampler, "wrapMode1", ANARI_STRING, wrap);
  anariSetParameter(dev, sc.sampler, "wrapMode2", ANARI_STRING, wrap);
  if (filter)
    anariSetParameter(dev, sc.sampler, "filter", ANARI_STRING, filter);
  if (flipV) {
    const float transform[16] = {
        1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float offset[4] = {0, 1, 0, 0};
    anariSetParameter(
        dev, sc.sampler, "inTransform", ANARI_FLOAT32_MAT4, transform);
    anariSetParameter(dev, sc.sampler, "inOffset", ANARI_FLOAT32_VEC4, offset);
  }
  anariCommitParameters(dev, sc.sampler);

  sc.mat = anariNewMaterial(dev, alphaMode ? "physicallyBased" : "matte");
  anariSetParameter(dev,
      sc.mat,
      alphaMode ? "baseColor" : "color",
      ANARI_SAMPLER,
      &sc.sampler);
  if (alphaMode) {
    anariSetParameter(dev, sc.mat, "alphaMode", ANARI_STRING, alphaMode);
    // Vela extracts alpha/metallic/roughness into the sampler's red output.
    const uint8_t packed[4] = {32, 96, 160, 224};
    auto image = anariNewArray2D(
        dev, packed, nullptr, nullptr, ANARI_UFIXED8_VEC4, 1, 1);
    auto opacity = anariNewSampler(dev, "image2D");
    anariSetParameter(dev, opacity, "image", ANARI_ARRAY2D, &image);
    float transform[16] = {};
    transform[4 * opacityChannel] = 0.5f;
    transform[15] = 1.f;
    const float offset[4] = {0.125f, 0, 0, 0};
    anariSetParameter(
        dev, opacity, "outTransform", ANARI_FLOAT32_MAT4, transform);
    anariSetParameter(dev, opacity, "outOffset", ANARI_FLOAT32_VEC4, offset);
    anariCommitParameters(dev, opacity);
    anariSetParameter(dev, sc.mat, "opacity", ANARI_SAMPLER, &opacity);
    anariRelease(dev, opacity);
    anariRelease(dev, image);
  }
  anariCommitParameters(dev, sc.mat);

  // A quad with st spanning the unit square (vertex.attribute0).
  static float verts[12] = {
      -0.9f, -0.9f, 0.f, 0.9f, -0.9f, 0.f, 0.9f, 0.9f, 0.f, -0.9f, 0.9f, 0.f};
  float st[8] = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
  sc.pos = anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  // Two triangles share positions, but carry six independent UV corners.
  float cornerSt[12] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
  sc.attr0 = anariNewArray1D(dev,
      g_faceVarying ? cornerSt : st,
      nullptr,
      nullptr,
      ANARI_FLOAT32_VEC2,
      g_faceVarying ? 6 : 4);
  sc.g = anariNewGeometry(dev, g_faceVarying ? "triangle" : "quad");
  anariSetParameter(dev, sc.g, "vertex.position", ANARI_ARRAY1D, &sc.pos);
  anariSetParameter(dev,
      sc.g,
      g_faceVarying ? "faceVarying.attribute0" : "vertex.attribute0",
      ANARI_ARRAY1D,
      &sc.attr0);
  if (g_faceVarying) {
    static uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    ANARIArray1D index =
        anariNewArray1D(dev, indices, nullptr, nullptr, ANARI_UINT32_VEC3, 2);
    anariSetParameter(dev, sc.g, "primitive.index", ANARI_ARRAY1D, &index);
    anariRelease(dev, index);
  }
  anariCommitParameters(dev, sc.g);

  sc.surf = anariNewSurface(dev);
  anariSetParameter(dev, sc.surf, "geometry", ANARI_GEOMETRY, &sc.g);
  anariSetParameter(dev, sc.surf, "material", ANARI_MATERIAL, &sc.mat);
  anariCommitParameters(dev, sc.surf);

  sc.world = anariNewWorld(dev);
  sc.surfArray =
      anariNewArray1D(dev, &sc.surf, nullptr, nullptr, ANARI_SURFACE, 1);
  anariSetParameter(dev, sc.world, "surface", ANARI_ARRAY1D, &sc.surfArray);
  anariCommitParameters(dev, sc.world);

  sc.cam = anariNewCamera(dev, "perspective");
  float eye[3] = {0.f, 0.f, 3.f};
  float dir[3] = {0.f, 0.f, -1.f};
  float up[3] = {0.f, 1.f, 0.f};
  float fovy = 0.6f;
  anariSetParameter(dev, sc.cam, "position", ANARI_FLOAT32_VEC3, eye);
  anariSetParameter(dev, sc.cam, "direction", ANARI_FLOAT32_VEC3, dir);
  anariSetParameter(dev, sc.cam, "up", ANARI_FLOAT32_VEC3, up);
  anariSetParameter(dev, sc.cam, "fovy", ANARI_FLOAT32, &fovy);
  anariCommitParameters(dev, sc.cam);

  sc.ren = anariNewRenderer(dev, "default");
  const float background[4] = {0, 0, 0, 0};
  anariSetParameter(dev, sc.ren, "background", ANARI_FLOAT32_VEC4, background);
  anariCommitParameters(dev, sc.ren);

  sc.frame = anariNewFrame(dev);
  uint32_t size[2] = {256, 256};
  ANARIDataType colorFmt = ANARI_UFIXED8_RGBA_SRGB;
  anariSetParameter(dev, sc.frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(dev, sc.frame, "channel.color", ANARI_DATA_TYPE, &colorFmt);
  anariSetParameter(dev, sc.frame, "world", ANARI_WORLD, &sc.world);
  anariSetParameter(dev, sc.frame, "camera", ANARI_CAMERA, &sc.cam);
  anariSetParameter(dev, sc.frame, "renderer", ANARI_RENDERER, &sc.ren);
  anariCommitParameters(dev, sc.frame);

  auto render = [&](Img &result) {
    anariRenderFrame(dev, sc.frame);
    anariFrameReady(dev, sc.frame, ANARI_WAIT);
    ANARIDataType pt = ANARI_UNKNOWN;
    const uint8_t *pixels = (const uint8_t *)anariMapFrame(
        dev, sc.frame, "channel.color", &result.w, &result.h, &pt);
    if (pixels)
      result.rgba.assign(pixels, pixels + size_t(result.w) * result.h * 4);
    anariUnmapFrame(dev, sc.frame, "channel.color");
  };
  render(im);
  if (mutated) {
    auto *uv = static_cast<float *>(anariMapArray(dev, sc.attr0));
    for (int i = 0; i < (g_faceVarying ? 6 : 4); ++i)
      uv[2 * i] = 1.f - uv[2 * i];
    anariUnmapArray(dev, sc.attr0);
    render(*mutated);
  }

  if (updates) {
    auto *uv = static_cast<float *>(anariMapArray(dev, sc.attr0));
    for (int i = 0; i < (g_faceVarying ? 6 : 4); ++i)
      uv[2 * i] = 1.f - uv[2 * i];
    anariUnmapArray(dev, sc.attr0);
    Img baseline;
    render(baseline);
    check(baseline.at(64, 64)[0] > baseline.at(64, 64)[1] + 30,
        "sampler update baseline is red");
    // A second material in another world exercises independent mirror
    // consumers.
    auto sharedMat = anariNewMaterial(dev, "matte");
    anariSetParameter(dev, sharedMat, "color", ANARI_SAMPLER, &sc.sampler);
    anariCommitParameters(dev, sharedMat);
    auto sharedSurface = anariNewSurface(dev);
    anariSetParameter(dev, sharedSurface, "geometry", ANARI_GEOMETRY, &sc.g);
    anariSetParameter(
        dev, sharedSurface, "material", ANARI_MATERIAL, &sharedMat);
    anariCommitParameters(dev, sharedSurface);
    auto sharedArray = anariNewArray1D(
        dev, &sharedSurface, nullptr, nullptr, ANARI_SURFACE, 1);
    auto sharedWorld = anariNewWorld(dev);
    anariSetParameter(dev, sharedWorld, "surface", ANARI_ARRAY1D, &sharedArray);
    anariCommitParameters(dev, sharedWorld);
    auto originalFrame = sc.frame;
    auto sharedFrame = anariNewFrame(dev);
    anariSetParameter(dev, sharedFrame, "size", ANARI_UINT32_VEC2, size);
    anariSetParameter(
        dev, sharedFrame, "channel.color", ANARI_DATA_TYPE, &colorFmt);
    anariSetParameter(dev, sharedFrame, "world", ANARI_WORLD, &sharedWorld);
    anariSetParameter(dev, sharedFrame, "camera", ANARI_CAMERA, &sc.cam);
    anariSetParameter(dev, sharedFrame, "renderer", ANARI_RENDERER, &sc.ren);
    anariCommitParameters(dev, sharedFrame);
    Img shared;
    sc.frame = sharedFrame;
    render(shared);
    check(shared.rgba == baseline.rgba, "shared sampler baseline matches");
    sc.frame = originalFrame;
    const float zero[16] = {};
    const float green[4] = {0, 1, 0, 1};
    anariSetParameter(
        dev, sc.sampler, "outTransform", ANARI_FLOAT32_MAT4, zero);
    anariSetParameter(dev, sc.sampler, "outOffset", ANARI_FLOAT32_VEC4, green);
    anariCommitParameters(dev, sc.sampler);
    Img changed;
    render(changed);
    const auto *pixel = changed.at(64, 64);
    check(pixel[1] > pixel[0] + 30 && pixel[1] > pixel[2] + 30,
        "same sampler output transform/offset edits change pixels");
    sc.frame = sharedFrame;
    render(shared);
    check(shared.rgba == changed.rgba,
        "shared sampler edit reaches another material and world");
    sc.frame = originalFrame;

    anariUnsetParameter(dev, sc.sampler, "outTransform");
    anariUnsetParameter(dev, sc.sampler, "outOffset");
    anariCommitParameters(dev, sc.sampler);
    render(changed);
    check(changed.rgba == baseline.rgba,
        "removing output parameters restores their defaults");

    anariSetParameter(dev, sc.sampler, "filter", ANARI_STRING, "nearest");
    anariCommitParameters(dev, sc.sampler);
    render(changed);
    check(countBlended(changed, 64, 16, 239)
            < countBlended(baseline, 64, 16, 239),
        "same sampler filter edit changes interpolation");
    anariUnsetParameter(dev, sc.sampler, "filter");
    anariCommitParameters(dev, sc.sampler);
    render(changed);
    check(changed.rgba == baseline.rgba, "filter removal restores linear");

    const float offset[4] = {1, 1, 0, 0};
    anariSetParameter(dev, sc.sampler, "inOffset", ANARI_FLOAT32_VEC4, offset);
    anariCommitParameters(dev, sc.sampler);
    render(changed);
    Img clamped = changed;
    anariSetParameter(dev, sc.sampler, "wrapMode1", ANARI_STRING, "repeat");
    anariSetParameter(dev, sc.sampler, "wrapMode2", ANARI_STRING, "repeat");
    anariCommitParameters(dev, sc.sampler);
    render(changed);
    pixel = changed.at(64, 64);
    check(changed.rgba != clamped.rgba && pixel[0] > pixel[1] + 30,
        "same sampler wrap edits repeat instead of clamping");
    anariUnsetParameter(dev, sc.sampler, "wrapMode1");
    anariUnsetParameter(dev, sc.sampler, "wrapMode2");
    anariCommitParameters(dev, sc.sampler);
    render(changed);
    check(changed.rgba == clamped.rgba, "wrap removal restores clamp");
    anariUnsetParameter(dev, sc.sampler, "inOffset");

    uint8_t blue[16] = {
        0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};
    auto replacement =
        anariNewArray2D(dev, blue, nullptr, nullptr, ANARI_UFIXED8_VEC4, 2, 2);
    anariSetParameter(dev, sc.sampler, "image", ANARI_ARRAY2D, &replacement);
    anariCommitParameters(dev, sc.sampler);
    render(changed);
    pixel = changed.at(64, 64);
    check(pixel[2] > pixel[0] + 30 && pixel[2] > pixel[1] + 30,
        "image replacement changes existing texture");
    auto *pixels = static_cast<uint8_t *>(anariMapArray(dev, replacement));
    for (int i = 0; i < 4; ++i) {
      pixels[4 * i + 1] = 255;
      pixels[4 * i + 2] = 0;
    }
    anariUnmapArray(dev, replacement);
    render(changed);
    pixel = changed.at(64, 64);
    check(pixel[1] > pixel[0] + 30 && pixel[1] > pixel[2] + 30,
        "image unmap refreshes texture without sampler recommit");
    sc.frame = sharedFrame;
    render(shared);
    check(shared.rgba == changed.rgba, "shared sampler sees image unmap");
    for (int i = 0; i < 5; ++i) {
      render(shared);
      check(shared.rgba == changed.rgba, "unchanged frame stays identical");
    }
    sc.frame = originalFrame;
    anariRelease(dev, replacement);
    anariRelease(dev, sharedFrame);
    anariRelease(dev, sharedWorld);
    anariRelease(dev, sharedArray);
    anariRelease(dev, sharedSurface);
    anariRelease(dev, sharedMat);
  }

  anariRelease(dev, sc.frame);
  anariRelease(dev, sc.ren);
  anariRelease(dev, sc.cam);
  anariRelease(dev, sc.surfArray);
  anariRelease(dev, sc.world);
  anariRelease(dev, sc.surf);
  anariRelease(dev, sc.g);
  anariRelease(dev, sc.attr0);
  anariRelease(dev, sc.pos);
  anariRelease(dev, sc.mat);
  anariRelease(dev, sc.sampler);
  anariRelease(dev, sc.image);
}

// Covered bounding box of the quad.
static void bbox(const Img &im, int &x0, int &y0, int &x1, int &y1)
{
  x0 = im.w;
  y0 = im.h;
  x1 = 0;
  y1 = 0;
  for (uint32_t y = 0; y < im.h; ++y)
    for (uint32_t x = 0; x < im.w; ++x) {
      const uint8_t *p = im.at(x, y);
      if (p[0] > 20 || p[1] > 20 || p[2] > 20) {
        x0 = std::min<int>(x0, x);
        y0 = std::min<int>(y0, y);
        x1 = std::max<int>(x1, x);
        y1 = std::max<int>(y1, y);
      }
    }
}

int main(int argc, char **argv)
{
  const char *subtype = argc > 1 ? argv[1] : "storm";
  g_faceVarying = argc > 2 && std::strcmp(argv[2], "faceVarying") == 0;

  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  if (!lib)
    return 2;
  ANARIDevice dev = anariNewDevice(lib, subtype);
  if (!dev)
    return 2;

  // --- Default (linear) filter: the four quadrants render their checker color.
  Img im, mutated;
  renderChecker(dev, nullptr, im, &mutated, false, nullptr, -1, true);

  int x0, y0, x1, y1;
  bbox(im, x0, y0, x1, y1);
  check(x1 > x0 && y1 > y0, "textured quad is visible");
  const int mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
  const int qx = (x1 - x0) / 4, qy = (y1 - y0) / 4;
  const int r = std::max(2, std::min(qx, qy) / 2);
  double tl[3], tr[3], bl[3], br[3];
  // ANARI frame rows run bottom-up; image array row zero is sampled at v=0.
  windowAvg(im, mx - qx, my + qy, r, tl); // v=1,u=0 -> blue
  windowAvg(im, mx + qx, my + qy, r, tr); // v=1,u=1 -> white
  windowAvg(im, mx - qx, my - qy, r, bl); // v=0,u=0 -> red
  windowAvg(im, mx + qx, my - qy, r, br); // v=0,u=1 -> green

  std::printf(
      "  quadrants  TL(blue)=%.0f %.0f %.0f  TR(white)=%.0f %.0f %.0f\n",
      tl[0],
      tl[1],
      tl[2],
      tr[0],
      tr[1],
      tr[2]);
  std::printf(
      "             BL(red )=%.0f %.0f %.0f  BR(green)=%.0f %.0f %.0f\n",
      bl[0],
      bl[1],
      bl[2],
      br[0],
      br[1],
      br[2]);

  check(bl[0] > bl[1] + 30 && bl[0] > bl[2] + 30,
      "bottom-left samples red (u=0,v=0)");
  check(br[1] > br[0] + 30 && br[1] > br[2] + 30,
      "bottom-right samples green (u=1,v=0)");
  check(tl[2] > tl[0] + 30 && tl[2] > tl[1] + 30,
      "top-left samples blue (u=0,v=1)");
  check(tr[0] > 120 && tr[1] > 120 && tr[2] > 120,
      "top-right samples white (u=1,v=1)");

  windowAvg(mutated, mx - qx, my - qy, r, bl);
  windowAvg(mutated, mx + qx, my - qy, r, br);
  check(bl[1] > bl[0] + 30 && bl[1] > bl[2] + 30 && br[0] > br[1] + 30
          && br[0] > br[2] + 30,
      "in-place UV mutation swaps the red and green quadrants");

  Img transformed;
  renderChecker(dev, "nearest", transformed, nullptr, true);
  windowAvg(transformed, mx - qx, my - qy, r, bl);
  windowAvg(transformed, mx - qx, my + qy, r, tl);
  check(bl[2] > bl[0] + 30 && bl[2] > bl[1] + 30 && tl[0] > tl[1] + 30
          && tl[0] > tl[2] + 30,
      "sampler input transform reverses V before conversion to USD coordinates");

  // --- filter="nearest" vs "linear": the red->green transition along the
  // bottom-row scanline snaps (nearest) vs spreads across the texel-center span
  // (linear). Count blended pixels on that scanline; nearest must be sharp.
  // Sample the scanline at the center of the v=0 row (same y the BL/BR quadrant
  // checks use), spanning the quad's covered width.
  const int scanY = my - qy;

  Img imN;
  renderChecker(dev, "nearest", imN);
  int nx0, ny0, nx1, ny1;
  bbox(imN, nx0, ny0, nx1, ny1);
  const int nearestBlend = countBlended(imN, scanY, nx0, nx1);

  Img imL;
  renderChecker(dev, "linear", imL);
  int lx0, ly0, lx1, ly1;
  bbox(imL, lx0, ly0, lx1, ly1);
  const int linearBlend = countBlended(imL, scanY, lx0, lx1);

  std::printf("  scanline blend px  nearest=%d  linear=%d  (quad width ~%d)\n",
      nearestBlend,
      linearBlend,
      nx1 - nx0);

  check(nearestBlend < 4,
      "nearest snaps: few blended px across the u=0.5 boundary");
  check(linearBlend > 10,
      "linear interpolates: wide blend band across the u=0.5 boundary");

  // Alpha modes must apply to sampler connections as well as constants.
  // Each channel has a distinct value, so dropping a swizzle cannot pass.
  for (int channel = 0; channel < 4; ++channel) {
    Img opaque, blend, mask;
    renderChecker(dev, "nearest", opaque, nullptr, false, "opaque", channel);
    renderChecker(dev, "nearest", blend, nullptr, false, "blend", channel);
    renderChecker(dev, "nearest", mask, nullptr, false, "mask", channel);
    const int expected = (32 + 64 * channel) / 2 + 32;
    const int actual = blend.at(128, 128)[3];
    std::printf("  channel %d: opaque=%u blend=%d expected=%d mask=%u\n",
        channel,
        opaque.at(128, 128)[3],
        actual,
        expected,
        mask.at(128, 128)[3]);
    check(opaque.at(128, 128)[3] == 255,
        "opaque mode ignores sampler-bound opacity");
    check(actual >= expected - 2 && actual <= expected + 2,
        "blend uses selected texture channel with output scale and offset");
    check(mask.at(128, 128)[3] == (expected < 128 ? 0 : 255),
        "mask thresholds the transformed texture channel");
  }

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
      g_errors);
  return g_errors == 0 ? 0 : 1;
}
