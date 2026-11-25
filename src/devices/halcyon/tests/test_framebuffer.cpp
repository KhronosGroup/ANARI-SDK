// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Frame channel encoding: renders an empty world, so every pixel shows the
// renderer's background color, and checks the 8-bit color formats (exact
// sRGB curve for UFIXED8_RGBA_SRGB, linear alpha, rounding to the nearest
// code in both 8-bit formats) and the depth of missed rays (FLT_MAX, like
// helide).

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using namespace anari::math;

// not a multiple of any ISPC program count, to exercise partial SIMD chunks
constexpr uint2 IMAGE_SIZE = {5, 3};

anari::Frame makeFrame(anari::Device d, anari::World world, float4 background)
{
  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 2.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::commitParameters(d, camera);

  auto renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::setParameter(d, renderer, "background", background);
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  return frame;
}

// Renders a frame whose background is 'background' with color format
// 'format' and returns the packed 8-bit RGBA pixels
std::vector<uint32_t> render8bit(anari::Device d,
    anari::World world,
    const char *test,
    ANARIDataType format,
    float4 background)
{
  auto frame = makeFrame(d, world, background);
  anari::setParameter(d, frame, "channel.color", format);
  anari::commitParameters(d, frame);
  anari::render(d, frame);
  anari::wait(d, frame);

  std::vector<uint32_t> pixels;
  auto fb = anari::map<uint32_t>(d, frame, "channel.color");
  if (fb.data && fb.width == IMAGE_SIZE.x && fb.height == IMAGE_SIZE.y)
    pixels.assign(fb.data, fb.data + fb.width * fb.height);
  else
    std::printf("FAIL %s: could not map channel.color\n", test);
  anari::unmap(d, frame, "channel.color");
  anari::release(d, frame);
  return pixels;
}

// An input channel value and the 8-bit code it must encode to
struct EncodeCase
{
  float value;
  uint8_t expected;
};

// Every pixel of a background (v, v, v, alpha) render in 'format' must be
// (code, code, code, alphaCode)
bool checkCodes(anari::Device d,
    anari::World world,
    const char *test,
    ANARIDataType format,
    const std::vector<EncodeCase> &codes,
    EncodeCase alpha)
{
  bool pass = true;
  for (const auto &c : codes) {
    const auto pixels = render8bit(
        d, world, test, format, float4(c.value, c.value, c.value, alpha.value));
    const uint32_t expected = c.expected | (c.expected << 8)
        | (c.expected << 16) | (uint32_t(alpha.expected) << 24);
    bool ok = !pixels.empty();
    for (uint32_t p : pixels)
      ok &= p == expected;
    if (!ok) {
      std::printf("FAIL %s: %g encodes as 0x%08x, expected 0x%08x\n",
          test,
          c.value,
          pixels.empty() ? 0u : pixels[0],
          expected);
    }
    pass &= ok;
  }
  std::printf("%s %s\n", pass ? "PASS" : "FAIL", test);
  return pass;
}

bool testSRGB(anari::Device d, anari::World world)
{
  // IEC 61966-2-1: 12.92 v up to 0.0031308, else 1.055 v^(1/2.4) - 0.055,
  // times 255, rounded (pow(v, 1/2.2) truncated gives 0, 11, 43, 122, 186,
  // 230, 255, 255, 0)
  const std::vector<EncodeCase> codes = {{0.f, 0},
      {0.001f, 3}, // linear segment
      {0.02f, 39},
      {0.2f, 124},
      {0.5f, 188},
      {0.8f, 231},
      {1.f, 255},
      {2.f, 255}, // clamped
      {-1.f, 0}};
  // alpha stays linear: 0.65 * 255 = 165.75
  return checkCodes(d,
      world,
      "UFIXED8_RGBA_SRGB uses the exact sRGB curve, rounded, linear alpha",
      ANARI_UFIXED8_RGBA_SRGB,
      codes,
      {0.65f, 166});
}

bool testLinear8(anari::Device d, anari::World world)
{
  // v * 255 rounded to nearest (truncation gives 63 and 165 for 0.25 and 0.65)
  const std::vector<EncodeCase> codes = {{0.f, 0},
      {0.25f, 64},
      {0.35f, 89},
      {0.4f, 102},
      {0.65f, 166},
      {1.f, 255},
      {2.f, 255},
      {-1.f, 0}};
  return checkCodes(d,
      world,
      "UFIXED8_VEC4 rounds to the nearest code",
      ANARI_UFIXED8_VEC4,
      codes,
      {0.65f, 166});
}

bool testMissDepth(anari::Device d, anari::World world)
{
  const char *test = "missed rays write depth FLT_MAX";
  auto frame = makeFrame(d, world, float4(0.f, 0.f, 0.f, 1.f));
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "channel.depth", ANARI_FLOAT32);
  anari::commitParameters(d, frame);
  anari::render(d, frame);
  anari::wait(d, frame);

  bool pass = true;
  auto fb = anari::map<float>(d, frame, "channel.depth");
  if (!fb.data || fb.width != IMAGE_SIZE.x || fb.height != IMAGE_SIZE.y) {
    std::printf("FAIL %s: could not map channel.depth\n", test);
    pass = false;
  } else {
    for (uint32_t i = 0; i < fb.width * fb.height; i++) {
      if (fb.data[i] != FLT_MAX) {
        std::printf("FAIL %s: pixel %u has depth %g\n", test, i, fb.data[i]);
        pass = false;
        break;
      }
    }
  }
  anari::unmap(d, frame, "channel.depth");
  anari::release(d, frame);
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

  auto world = anari::newObject<anari::World>(d);
  anari::commitParameters(d, world);

  bool pass = true;
  pass &= testSRGB(d, world);
  pass &= testLinear8(d, world);
  pass &= testMissDepth(d, world);
  anari::release(d, world);

  anari::release(d, d);
  anari::unloadLibrary(library);

  if (g_deviceError)
    std::printf("FAIL: device reported errors\n");

  return pass && !g_deviceError ? 0 : 1;
}
