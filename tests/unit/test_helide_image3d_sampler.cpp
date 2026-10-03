// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Regression for helide's image3D sampler offsets: per the sampler spec it
// samples the image at inTransform * inAttribute + 'inOffset' and returns
// outTransform * sample + 'outOffset'. A triangle colored through an offset
// sampler must shade the same as through an unoffset sampler whose attribute
// (for inOffset) or image (for outOffset) is pre-offset on the host.

#include "catch.hpp"
#include "helide_render_test.h"

// std
#include <cmath>
#include <functional>
#include <vector>

namespace {

using namespace helide_test;

constexpr uint3 kImageDims = {4, 4, 4};

const std::vector<float3> kTriangle = {
    {-0.8f, -0.8f, 0.f}, {0.8f, -0.8f, 0.f}, {0.f, 0.8f, 0.f}};

// image coordinates, kept away from the edges so offsets don't clamp
const std::vector<float4> kAttribute = {{0.15f, 0.2f, 0.3f, 0.f},
    {0.6f, 0.3f, 0.55f, 0.f},
    {0.35f, 0.65f, 0.2f, 0.f}};

const float4 kInOffset = {0.2f, -0.1f, 0.15f, 0.f};
const float4 kOutOffset = {0.3f, 0.2f, 0.1f, 0.f};

// Texel values vary nonlinearly so a shifted lookup changes the color.
std::vector<float4> makeImage()
{
  std::vector<float4> image;
  for (uint32_t k = 0; k < kImageDims.z; k++)
    for (uint32_t j = 0; j < kImageDims.y; j++)
      for (uint32_t i = 0; i < kImageDims.x; i++) {
        const float x = i * 0.37f + j * 0.11f + k * 0.23f;
        image.push_back(float4(0.6f * std::abs(std::sin(3.f * x)),
            0.6f * std::abs(std::cos(2.f * x + k)),
            0.6f * std::abs(std::sin(x * j + 0.5f)),
            1.f));
      }
  return image;
}

const std::vector<float4> kImage = makeImage();

using SetSamplerParams = std::function<void(anari::Device, anari::Sampler)>;

// The material color is an image3D sampler over 'image' looked up by
// attribute0, with extra parameters set by 'setSamplerParams' (if any).
anari::World makeWorld(anari::Device d,
    const std::vector<float4> &attribute,
    const std::vector<float4> &image,
    const SetSamplerParams &setSamplerParams)
{
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(
      d, geom, "vertex.position", kTriangle.data(), kTriangle.size());
  anari::setParameterArray1D(
      d, geom, "vertex.attribute0", attribute.data(), attribute.size());
  anari::commitParameters(d, geom);

  auto sampler = anari::newObject<anari::Sampler>(d, "image3D");
  anari::setParameterArray3D(d,
      sampler,
      "image",
      image.data(),
      kImageDims.x,
      kImageDims.y,
      kImageDims.z);
  anari::setParameter(d, sampler, "inAttribute", "attribute0");
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

std::vector<float4> renderWorld(anari::Device d,
    const std::vector<float4> &attribute,
    const std::vector<float4> &image,
    const SetSamplerParams &setSamplerParams)
{
  auto world = makeWorld(d, attribute, image, setSamplerParams);
  auto pixels = render(d, world);
  anari::release(d, world);
  return pixels;
}

std::vector<float4> offsetBy(const std::vector<float4> &v, const float4 &o)
{
  std::vector<float4> r;
  for (auto &a : v)
    r.push_back(a + o);
  return r;
}

// The offset sampler render must match the render with the pre-offset
// attribute and image, and that expected image must differ from an unoffset
// sampler (so the check can't pass by the offsets being ignored).
void checkSamplerMatches(anari::Device d,
    const SetSamplerParams &setSamplerParams,
    const std::vector<float4> &expectedAttribute,
    const std::vector<float4> &expectedImage)
{
  const auto sampled = renderWorld(d, kAttribute, kImage, setSamplerParams);
  const auto expected = renderWorld(d, expectedAttribute, expectedImage, {});
  const auto unoffset = renderWorld(d, kAttribute, kImage, {});

  const size_t pixels = sampled.size();
  const size_t changed = countMismatches(expected, unoffset);
  const size_t mismatches = countMismatches(sampled, expected);

  INFO(changed << " pixels changed by the offsets, " << mismatches
               << " mismatches");
  CHECK(changed > pixels / 10);
  CHECK(mismatches == 0);
}

} // namespace

TEST_CASE("image3D sampler reads inOffset and outOffset",
    "[helide][helide_image3d_sampler]")
{
  anari::Library lib = anari::loadLibrary("helide", printErrors, nullptr);
  if (lib == nullptr) {
    WARN("helide library not available; skipping image3D sampler test");
    return;
  }

  anari::Device d = anari::newDevice(lib, "default");

  SECTION("inOffset")
  {
    checkSamplerMatches(
        d,
        [](anari::Device d, anari::Sampler s) {
          anari::setParameter(d, s, "inOffset", kInOffset);
        },
        offsetBy(kAttribute, kInOffset),
        kImage);
  }

  SECTION("outOffset")
  {
    checkSamplerMatches(
        d,
        [](anari::Device d, anari::Sampler s) {
          anari::setParameter(d, s, "outOffset", kOutOffset);
        },
        kAttribute,
        offsetBy(kImage, kOutOffset));
  }

  SECTION("inOffset and outOffset")
  {
    checkSamplerMatches(
        d,
        [](anari::Device d, anari::Sampler s) {
          anari::setParameter(d, s, "inOffset", kInOffset);
          anari::setParameter(d, s, "outOffset", kOutOffset);
        },
        offsetBy(kAttribute, kInOffset),
        offsetBy(kImage, kOutOffset));
  }

  anari::release(d, d);
  anari::unloadLibrary(lib);
}
