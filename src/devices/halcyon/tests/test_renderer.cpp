// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Renders a quad tilted 60 degrees away from an orthographic camera (so the
// eye-light falloff is exactly 0.5) and checks the renderer's shading
// parameters, alpha compositing and task grain sizes against the float color
// channel.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace {

using namespace anari::math;

// not a multiple of any ISPC program count, to exercise partial SIMD chunks
constexpr uint2 IMAGE_SIZE = {37, 37};
constexpr float FALLOFF = 0.5f; // cos(60 deg)
const float3 MATERIAL_COLOR = {0.8f, 0.4f, 0.2f};

// A quad in the plane through (-0.5, 0, 0) with normal (sin 60, 0, cos 60),
// covering the left half of the frame
anari::World makeWorld(anari::Device d, float3 color)
{
  const float t = std::tan(60.f * 3.14159265f / 180.f);
  auto z = [&](float x) { return -t * (x + 0.5f); };
  const std::vector<float3> verts = {{-0.9f, -0.9f, z(-0.9f)},
      {-0.1f, -0.9f, z(-0.1f)},
      {-0.1f, 0.9f, z(-0.1f)},
      {-0.9f, 0.9f, z(-0.9f)}};
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};

  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(
      d, geom, "vertex.position", verts.data(), verts.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", color);
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::commitParameters(d, world);
  anari::release(d, surface);
  return world;
}

// The whole camera image, as the 'imageRegion' box (lower x, y, upper x, y)
const float4 FULL_REGION = {0.f, 0.f, 1.f, 1.f};

// Orthographic camera looking down -z at the [-1, 1]^2 square, rendering the
// part 'imageRegion' of it
anari::Camera makeCamera(anari::Device d, float4 imageRegion = FULL_REGION)
{
  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(
      d, camera, "imageRegion", ANARI_FLOAT32_BOX2, &imageRegion);
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 2.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", 2.f);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);
  return camera;
}

using SetParams = std::function<void(anari::Device, anari::Renderer)>;

// Renders 'world' with a "preview" renderer configured by 'setParams' (and
// committed unless 'commit' is false) through a camera showing 'imageRegion',
// returning the float color channel
std::vector<float4> render(anari::Device d,
    anari::World world,
    const SetParams &setParams,
    bool commit = true,
    float4 imageRegion = FULL_REGION)
{
  auto renderer = anari::newObject<anari::Renderer>(d, "preview");
  if (setParams)
    setParams(d, renderer);
  if (commit)
    anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setAndReleaseParameter(d, frame, "camera", makeCamera(d, imageRegion));
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  anari::render(d, frame);
  anari::wait(d, frame);

  std::vector<float4> pixels;
  auto fb = anari::map<float4>(d, frame, "channel.color");
  if (fb.data && fb.width == IMAGE_SIZE.x && fb.height == IMAGE_SIZE.y)
    pixels.assign(fb.data, fb.data + fb.width * fb.height);
  else
    std::printf("FAIL: could not map channel.color\n");
  anari::unmap(d, frame, "channel.color");
  anari::release(d, frame);
  return pixels;
}

size_t pixelIndex(float2 pos)
{
  const auto x = uint32_t((pos.x + 1.f) / 2.f * IMAGE_SIZE.x);
  const auto y = uint32_t((pos.y + 1.f) / 2.f * IMAGE_SIZE.y);
  return size_t(y) * IMAGE_SIZE.x + x;
}

const float2 HIT_POS = {-0.5f, 0.f};
const float2 MISS_POS = {0.5f, 0.f};

bool near(float4 a, float4 b)
{
  const float4 d = linalg::abs(a - b);
  return d.x < 1e-4f && d.y < 1e-4f && d.z < 1e-4f && d.w < 1e-4f;
}

// Checks pixel 'i', reporting a mismatch as 'what' in 'test'
bool checkPixel(const char *test,
    const char *what,
    const std::vector<float4> &pixels,
    size_t i,
    float4 expected)
{
  if (pixels.empty())
    return false;
  const float4 got = pixels[i];
  if (near(got, expected))
    return true;
  std::printf("FAIL %s, %s: got (%f, %f, %f, %f), expected (%f, %f, %f, %f)\n",
      test,
      what,
      got.x,
      got.y,
      got.z,
      got.w,
      expected.x,
      expected.y,
      expected.z,
      expected.w);
  return false;
}

bool checkPixel(const char *test,
    const char *what,
    const std::vector<float4> &pixels,
    float2 pos,
    float4 expected)
{
  return checkPixel(test, what, pixels, pixelIndex(pos), expected);
}

// Checks the hit and missed pixel of an image rendered with 'setParams'
bool check(anari::Device d,
    anari::World world,
    const char *test,
    const SetParams &setParams,
    float4 expectedHit,
    float4 expectedMiss,
    bool commit = true)
{
  const auto pixels = render(d, world, setParams, commit);
  bool pass = checkPixel(test, "hit", pixels, HIT_POS, expectedHit);
  pass &= checkPixel(test, "miss", pixels, MISS_POS, expectedMiss);
  std::printf("%s %s\n", pass ? "PASS" : "FAIL", test);
  return pass;
}

float4 shaded(float3 c, float ratio, float ambient = 1.f)
{
  return float4((ratio * c * FALLOFF + (1.f - ratio) * c) * ambient, 1.f);
}

bool testShading(anari::Device d, anari::World world)
{
  const float3 c = MATERIAL_COLOR;
  const float4 black(0.f, 0.f, 0.f, 1.f);

  bool pass = true;
  pass &= check(d, world, "defaults", nullptr, shaded(c, 0.5f), black);
  pass &= check(
      d, world, "uncommitted renderer", nullptr, shaded(c, 0.5f), black, false);
  pass &= check(
      d,
      world,
      "eyeLightBlendRatio 0",
      [](auto d, auto r) {
        anari::setParameter(d, r, "eyeLightBlendRatio", 0.f);
      },
      shaded(c, 0.f),
      black);
  pass &= check(
      d,
      world,
      "eyeLightBlendRatio 1",
      [](auto d, auto r) {
        anari::setParameter(d, r, "eyeLightBlendRatio", 1.f);
      },
      shaded(c, 1.f),
      black);
  pass &= check(
      d,
      world,
      "ambientRadiance ignored by default",
      [](auto d, auto r) {
        anari::setParameter(d, r, "ambientRadiance", 0.5f);
      },
      shaded(c, 0.5f),
      black);
  pass &= check(
      d,
      world,
      "ambientRadiance",
      [](auto d, auto r) {
        anari::setParameter(d, r, "ambientRadiance", 0.5f);
        anari::setParameter(d, r, "ignoreAmbientLighting", false);
      },
      shaded(c, 0.5f, 0.5f),
      black);
  // khr_renderer_ambient_light: 'ambientRadiance' defaults to 0
  pass &= check(
      d,
      world,
      "ambientRadiance default",
      [](auto d, auto r) {
        anari::setParameter(d, r, "ignoreAmbientLighting", false);
      },
      shaded(c, 0.5f, 0.f),
      black);
  pass &= check(
      d,
      world,
      "transparent background",
      [](auto d, auto r) {
        anari::setParameter(d, r, "background", float4(0.2f, 0.3f, 0.4f, 0.f));
      },
      shaded(c, 0.5f),
      float4(0.2f, 0.3f, 0.4f, 0.f));
  pass &= check(
      d,
      world,
      "translucent background",
      [](auto d, auto r) {
        anari::setParameter(d, r, "background", float4(0.2f, 0.3f, 0.4f, 0.5f));
      },
      shaded(c, 0.5f),
      float4(0.2f, 0.3f, 0.4f, 0.5f));
  return pass;
}

// Surface colors above 1 are clamped to 1 before compositing. The world is
// created after other worlds have rendered, which must not disturb them.
bool testColorClamp(anari::Device d)
{
  auto world = makeWorld(d, float3(2.f, 0.5f, 3.f));
  const bool pass = check(
      d,
      world,
      "geometry color clamp",
      [](auto d, auto r) {
        anari::setParameter(d, r, "eyeLightBlendRatio", 0.f);
      },
      float4(1.f, 0.5f, 1.f, 1.f),
      float4(0.f, 0.f, 0.f, 1.f));
  anari::release(d, world);
  return pass;
}

// Any task grain size renders every pixel exactly as the default one does
bool testTaskGrainSize(anari::Device d, anari::World world)
{
  const auto reference = render(d, world, nullptr);
  if (reference.empty())
    return false;

  bool pass = true;
  for (int2 grain : {int2(1, 1), int2(3, 5), int2(128, 128), int2(200, 0)}) {
    const auto pixels = render(d, world, [&](auto d, auto r) {
      anari::setParameter(d, r, "taskGrainSizeWidth", grain.x);
      anari::setParameter(d, r, "taskGrainSizeHeight", grain.y);
    });
    if (pixels != reference) {
      std::printf(
          "FAIL taskGrainSize %dx%d: image differs\n", grain.x, grain.y);
      pass = false;
    }
  }
  std::printf("%s taskGrainSize\n", pass ? "PASS" : "FAIL");
  return pass;
}

// Background image texels, row by row from the bottom; the alphas vary so
// compositing shows up too
constexpr uint2 BG_IMAGE_SIZE = {3, 2};
const std::vector<float4> BG_IMAGE = {{1.f, 0.f, 0.f, 1.f},
    {0.f, 1.f, 0.f, 0.5f},
    {0.f, 0.f, 1.f, 1.f},
    {1.f, 1.f, 0.f, 0.f},
    {0.f, 1.f, 1.f, 1.f},
    {1.f, 0.f, 1.f, 0.25f}};

// Bilinear lookup of BG_IMAGE at 'screen' with texel centers at half-texel
// offsets and edges clamped, as helide's backgroundColorFromImage()
float4 backgroundAt(float2 screen)
{
  auto texel = [](int x, int y) {
    x = std::clamp(x, 0, int(BG_IMAGE_SIZE.x) - 1);
    y = std::clamp(y, 0, int(BG_IMAGE_SIZE.y) - 1);
    return BG_IMAGE[y * BG_IMAGE_SIZE.x + x];
  };
  const float fx = screen.x * BG_IMAGE_SIZE.x - 0.5f;
  const float fy = screen.y * BG_IMAGE_SIZE.y - 0.5f;
  const int x = int(std::floor(fx));
  const int y = int(std::floor(fy));
  const float tx = fx - x;
  const float ty = fy - y;
  const float4 v0 = lerp(texel(x, y), texel(x, y + 1), ty);
  const float4 v1 = lerp(texel(x + 1, y), texel(x + 1, y + 1), ty);
  return lerp(v0, v1, tx);
}

// Checks every missed pixel against a bilinear background image lookup at the
// pixel's screen position within 'imageRegion'; surfaces stay opaque over it
bool testBackgroundImage(
    anari::Device d, anari::World world, const char *test, float4 imageRegion)
{
  const auto pixels = render(
      d,
      world,
      [](auto d, auto r) {
        anari::setParameterArray2D(d,
            r,
            "background",
            BG_IMAGE.data(),
            BG_IMAGE_SIZE.x,
            BG_IMAGE_SIZE.y);
      },
      true,
      imageRegion);
  if (pixels.empty())
    return false;

  bool pass =
      checkPixel(test, "hit", pixels, HIT_POS, shaded(MATERIAL_COLOR, 0.5f));
  // independent of backgroundAt(): the top right pixel is past the last
  // texel centre on both axes, so it clamps to exactly the last texel
  if (imageRegion == FULL_REGION) {
    pass &= checkPixel(
        test, "top right pixel", pixels, pixels.size() - 1, BG_IMAGE.back());
  }
  for (uint32_t y = 0; y < IMAGE_SIZE.y && pass; y++) {
    for (uint32_t x = IMAGE_SIZE.x / 2 + 1; x < IMAGE_SIZE.x && pass; x++) {
      // rays (and background lookups) go through pixel centres
      const float2 screen((x + 0.5f) / IMAGE_SIZE.x, (y + 0.5f) / IMAGE_SIZE.y);
      const float2 regionScreen(lerp(imageRegion.x, imageRegion.z, screen.x),
          lerp(imageRegion.y, imageRegion.w, screen.y));
      char what[32];
      std::snprintf(what, sizeof(what), "pixel (%u, %u)", x, y);
      pass &= checkPixel(test,
          what,
          pixels,
          size_t(y) * IMAGE_SIZE.x + x,
          backgroundAt(regionScreen));
    }
  }
  std::printf("%s %s\n", pass ? "PASS" : "FAIL", test);
  return pass;
}

// A background image the size of the frame lands texel for pixel: rays go
// through pixel centres, which are the texel centres, so pixel (x, y) shows
// exactly texel (x, y) with no blending of its neighbours
bool testBackgroundImageOneToOne(anari::Device d)
{
  const char *test = "background image 1:1";
  std::vector<float4> image(size_t(IMAGE_SIZE.x) * IMAGE_SIZE.y);
  for (uint32_t y = 0; y < IMAGE_SIZE.y; y++) {
    for (uint32_t x = 0; x < IMAGE_SIZE.x; x++) {
      // a checkerboard, so any blend with a neighbour changes the colour
      const float c = (x + y) % 2 ? 1.f : 0.f;
      image[size_t(y) * IMAGE_SIZE.x + x] = {c, 1.f - c, float(x % 2), 1.f};
    }
  }

  auto world = anari::newObject<anari::World>(d);
  anari::commitParameters(d, world);
  const auto pixels = render(d, world, [&](auto d, auto r) {
    anari::setParameterArray2D(
        d, r, "background", image.data(), IMAGE_SIZE.x, IMAGE_SIZE.y);
  });
  anari::release(d, world);

  bool pass = !pixels.empty();
  for (size_t i = 0; i < pixels.size() && pass; i++) {
    char what[32];
    std::snprintf(what,
        sizeof(what),
        "pixel (%zu, %zu)",
        i % IMAGE_SIZE.x,
        i / IMAGE_SIZE.x);
    pass &= checkPixel(test, what, pixels, i, image[i]);
  }
  std::printf("%s %s\n", pass ? "PASS" : "FAIL", test);
  return pass;
}

// device errors (e.g. ISPC object size mismatches) fail the test
bool g_deviceError = false;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_ERROR)
    g_deviceError = true;
  if (severity <= ANARI_SEVERITY_WARNING)
    std::fprintf(stderr, "[ANARI] %s\n", message);
}

} // namespace

int main()
{
  auto library = anari::loadLibrary("halcyon", statusFunc);
  if (!library) {
    std::fprintf(stderr, "failed to load halcyon library\n");
    return 1;
  }
  auto d = anari::newDevice(library, "default");

  auto world = makeWorld(d, MATERIAL_COLOR);

  bool pass = true;
  pass &= testShading(d, world);
  pass &= testColorClamp(d);
  pass &= testTaskGrainSize(d, world);
  pass &= testBackgroundImage(d, world, "background image", FULL_REGION);
  // the image is looked up in region coordinates, so only part of it shows
  pass &= testBackgroundImage(d,
      world,
      "background image with imageRegion",
      float4(0.25f, 0.25f, 1.f, 0.75f));
  pass &= testBackgroundImageOneToOne(d);
  anari::release(d, world);

  anari::release(d, d);
  anari::unloadLibrary(library);

  if (g_deviceError)
    std::printf("FAIL: device reported errors\n");

  return pass && !g_deviceError ? 0 : 1;
}
