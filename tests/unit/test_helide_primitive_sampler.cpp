// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Regression for helide's primitive sampler offset name: per
// khr_sampler_primitive.json the sampler reads array['inOffset' + primID].
// helide keeps reading its older 'offset' name as a fallback. Two triangles
// colored through an offset sampler must shade the same as through a sampler
// over the sub-array starting at that offset.

#include "catch.hpp"

#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

constexpr uint2 kImageSize = {64, 64};
constexpr uint64_t kOffset = 16;
constexpr uint64_t kLegacyOffset = 8;
// room for both triangles past the largest offset
constexpr int kNumColors = int(kOffset) + 4;

// Two triangles (primID 0 and 1) covering the left and right halves of the
// view.
const std::vector<float3> kTriangles = {{-0.9f, -0.9f, 0.f},
    {-0.1f, -0.9f, 0.f},
    {-0.5f, 0.9f, 0.f},
    {0.1f, -0.9f, 0.f},
    {0.9f, -0.9f, 0.f},
    {0.5f, 0.9f, 0.f}};

// Every element is distinct, so reading at the wrong offset changes pixels.
std::vector<float4> makeColors()
{
  std::vector<float4> colors;
  for (int i = 0; i < kNumColors; i++) {
    const float t = i / float(kNumColors);
    colors.push_back(float4(t, 1.f - t, 0.25f + 0.5f * t, 1.f));
  }
  return colors;
}

const std::vector<float4> kColors = makeColors();

using SetSamplerParams = std::function<void(anari::Device, anari::Sampler)>;

struct StatusCounts
{
  int inOffsetWarnings{0};
};

void statusFunc(const void *userData,
    ANARIDevice,
    ANARIObject source,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity == ANARI_SEVERITY_FATAL_ERROR
      || severity == ANARI_SEVERITY_ERROR) {
    fprintf(stderr, "[ANARI][ERROR][%p] %s\n", source, message);
  } else if (severity == ANARI_SEVERITY_WARNING && userData
      && std::string(message).find("'inOffset'") != std::string::npos) {
    ((StatusCounts *)userData)->inOffsetWarnings++;
  }
}

// The material color is a primitive sampler over 'colors', with extra
// parameters set by 'setSamplerParams' (if any).
anari::World makeWorld(anari::Device d,
    const std::vector<float4> &colors,
    const SetSamplerParams &setSamplerParams)
{
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(
      d, geom, "vertex.position", kTriangles.data(), kTriangles.size());
  anari::commitParameters(d, geom);

  auto sampler = anari::newObject<anari::Sampler>(d, "primitive");
  anari::setParameterArray1D(d, sampler, "array", colors.data(), colors.size());
  if (setSamplerParams)
    setSamplerParams(d, sampler);
  anari::commitParameters(d, sampler);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setAndReleaseParameter(d, mat, "color", sampler);
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

std::vector<float4> render(anari::Device d, anari::World world)
{
  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 2.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);

  auto renderer = anari::newObject<anari::Renderer>(d, "default");
  anari::setParameter(d, renderer, "background", float4(0.f, 0.f, 0.f, 1.f));
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", kImageSize);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  anari::render(d, frame);
  anari::wait(d, frame);

  auto fb = anari::map<float4>(d, frame, "channel.color");
  std::vector<float4> pixels(fb.data, fb.data + fb.width * fb.height);
  anari::unmap(d, frame, "channel.color");

  anari::release(d, frame);
  return pixels;
}

std::vector<float4> renderWorld(anari::Device d,
    const std::vector<float4> &colors,
    const SetSamplerParams &setSamplerParams)
{
  auto world = makeWorld(d, colors, setSamplerParams);
  auto pixels = render(d, world);
  anari::release(d, world);
  return pixels;
}

std::vector<float4> colorsFrom(uint64_t offset)
{
  return std::vector<float4>(kColors.begin() + offset, kColors.end());
}

size_t countMismatches(
    const std::vector<float4> &a, const std::vector<float4> &b)
{
  size_t mismatches = 0;
  for (size_t i = 0; i < a.size(); i++) {
    if (linalg::maxelem(linalg::abs(a[i] - b[i])) > 1e-3f)
      mismatches++;
  }
  return mismatches;
}

// The offset sampler render must match a render of the sub-array starting at
// 'expectedOffset', and that expected image must differ from an unoffset
// sampler (so the check can't pass by the offset being ignored).
void checkSamplerMatches(anari::Device d,
    const SetSamplerParams &setSamplerParams,
    uint64_t expectedOffset)
{
  const auto sampled = renderWorld(d, kColors, setSamplerParams);
  const auto expected = renderWorld(d, colorsFrom(expectedOffset), {});
  const auto unoffset = renderWorld(d, kColors, {});

  const size_t pixels = sampled.size();
  const size_t changed = countMismatches(expected, unoffset);
  const size_t mismatches = countMismatches(sampled, expected);

  INFO(changed << " pixels changed by the offset, " << mismatches
               << " mismatches");
  CHECK(changed > pixels / 10);
  CHECK(mismatches == 0);
}

} // namespace

TEST_CASE(
    "primitive sampler reads inOffset", "[helide][helide_primitive_sampler]")
{
  StatusCounts status;
  anari::Library lib = anari::loadLibrary("helide", statusFunc, &status);
  if (lib == nullptr) {
    WARN("helide library not available; skipping primitive sampler test");
    return;
  }

  anari::Device d = anari::newDevice(lib, "default");

  SECTION("inOffset")
  {
    checkSamplerMatches(
        d,
        [](anari::Device d, anari::Sampler s) {
          anari::setParameter(d, s, "inOffset", kOffset);
        },
        kOffset);
  }

  SECTION("legacy 'offset' is still read")
  {
    checkSamplerMatches(
        d,
        [](anari::Device d, anari::Sampler s) {
          anari::setParameter(d, s, "offset", kLegacyOffset);
        },
        kLegacyOffset);
  }

  SECTION("legacy 32-bit 'offset' is still read")
  {
    checkSamplerMatches(
        d,
        [](anari::Device d, anari::Sampler s) {
          anari::setParameter(d, s, "offset", uint32_t(kLegacyOffset));
        },
        kLegacyOffset);
  }

  SECTION("inOffset takes precedence over 'offset'")
  {
    checkSamplerMatches(
        d,
        [](anari::Device d, anari::Sampler s) {
          anari::setParameter(d, s, "offset", kLegacyOffset);
          anari::setParameter(d, s, "inOffset", kOffset);
        },
        kOffset);
  }

  SECTION("an inOffset of an unsupported type warns and is ignored")
  {
    const auto sampled =
        renderWorld(d, kColors, [](anari::Device d, anari::Sampler s) {
          anari::setParameter(d, s, "inOffset", 16.f);
        });
    const auto unoffset = renderWorld(d, kColors, {});
    CHECK(countMismatches(sampled, unoffset) == 0);
    CHECK(status.inOffsetWarnings > 0);
  }

  anari::release(d, d);
  anari::unloadLibrary(lib);
}
