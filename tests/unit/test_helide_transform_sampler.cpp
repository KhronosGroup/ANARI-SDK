// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Regression for helide's transform sampler parameter names: per
// khr_sampler_transform.json the sampler computes
// 'outTransform' * inAttribute + 'outOffset'. helide keeps reading its older
// 'transform' name as a fallback. A triangle colored through the sampler must
// shade the same as the triangle whose attribute0 is pre-transformed on the
// host and used directly as the material color.

#include "catch.hpp"
#include "helide_render_test.h"

// std
#include <functional>
#include <vector>

namespace {

using namespace helide_test;

const std::vector<float3> kTriangle = {
    {-0.8f, -0.8f, 0.f}, {0.8f, -0.8f, 0.f}, {0.f, 0.8f, 0.f}};

const std::vector<float4> kAttribute = {
    {0.2f, 0.1f, 0.3f, 1.f}, {0.4f, 0.3f, 0.1f, 1.f}, {0.1f, 0.4f, 0.2f, 1.f}};

// column-major: swizzles and scales the channels so the result is far from
// the identity but stays in [0, 1]
const mat4 kOutTransform = mat4(float4(0.f, 1.f, 0.f, 0.f),
    float4(0.f, 0.f, 1.5f, 0.f),
    float4(2.f, 0.f, 0.f, 0.f),
    float4(0.f, 0.f, 0.f, 1.f));
const float4 kOutOffset = {0.3f, 0.2f, 0.1f, 0.f};
const mat4 kLegacyTransform = mat4(float4(0.f, 0.f, 2.f, 0.f),
    float4(2.f, 0.f, 0.f, 0.f),
    float4(0.f, 2.f, 0.f, 0.f),
    float4(0.f, 0.f, 0.f, 1.f));

using SetSamplerParams = std::function<void(anari::Device, anari::Sampler)>;

// The material color is a transform sampler set up by 'setSamplerParams', or
// the string "attribute0" if 'setSamplerParams' is empty.
anari::World makeWorld(anari::Device d,
    const std::vector<float4> &attribute,
    const SetSamplerParams &setSamplerParams)
{
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(
      d, geom, "vertex.position", kTriangle.data(), kTriangle.size());
  anari::setParameterArray1D(
      d, geom, "vertex.attribute0", attribute.data(), attribute.size());
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  if (setSamplerParams) {
    auto sampler = anari::newObject<anari::Sampler>(d, "transform");
    anari::setParameter(d, sampler, "inAttribute", "attribute0");
    setSamplerParams(d, sampler);
    anari::commitParameters(d, sampler);
    anari::setAndReleaseParameter(d, mat, "color", sampler);
  } else {
    anari::setParameter(d, mat, "color", "attribute0");
  }
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
    const SetSamplerParams &setSamplerParams)
{
  auto world = makeWorld(d, attribute, setSamplerParams);
  auto pixels = render(d, world);
  anari::release(d, world);
  return pixels;
}

std::vector<float4> transformed(
    const std::vector<float4> &v, const mat4 &m, const float4 &offset)
{
  std::vector<float4> r;
  for (auto &a : v)
    r.push_back(linalg::mul(m, a) + offset);
  return r;
}

// The sampler render must match the pre-transformed-attribute render, and
// that expected image must differ from an identity sampler (so the check
// can't pass by the sampler being ignored).
void checkSamplerMatches(anari::Device d,
    const SetSamplerParams &setSamplerParams,
    const std::vector<float4> &expectedAttribute)
{
  const auto sampled = renderWorld(d, kAttribute, setSamplerParams);
  const auto expected = renderWorld(d, expectedAttribute, {});
  const auto identity = renderWorld(d, kAttribute, {});

  const size_t pixels = sampled.size();
  const size_t changed = countMismatches(expected, identity);
  const size_t mismatches = countMismatches(sampled, expected);

  INFO(changed << " pixels changed by the transform, " << mismatches
               << " mismatches");
  CHECK(changed > pixels / 10);
  CHECK(mismatches == 0);
}

} // namespace

TEST_CASE("transform sampler reads outTransform and outOffset",
    "[helide][helide_transform_sampler]")
{
  anari::Library lib = anari::loadLibrary("helide", printErrors, nullptr);
  if (lib == nullptr) {
    WARN("helide library not available; skipping transform sampler test");
    return;
  }

  anari::Device d = anari::newDevice(lib, "default");

  SECTION("outTransform")
  {
    checkSamplerMatches(
        d,
        [](anari::Device d, anari::Sampler s) {
          anari::setParameter(d, s, "outTransform", kOutTransform);
        },
        transformed(kAttribute, kOutTransform, float4(0.f)));
  }

  SECTION("outOffset")
  {
    checkSamplerMatches(
        d,
        [](anari::Device d, anari::Sampler s) {
          anari::setParameter(d, s, "outOffset", kOutOffset);
        },
        transformed(kAttribute, mat4(linalg::identity), kOutOffset));
  }

  SECTION("outTransform and outOffset")
  {
    checkSamplerMatches(
        d,
        [](anari::Device d, anari::Sampler s) {
          anari::setParameter(d, s, "outTransform", kOutTransform);
          anari::setParameter(d, s, "outOffset", kOutOffset);
        },
        transformed(kAttribute, kOutTransform, kOutOffset));
  }

  SECTION("legacy 'transform' is still read")
  {
    checkSamplerMatches(
        d,
        [](anari::Device d, anari::Sampler s) {
          anari::setParameter(d, s, "transform", kLegacyTransform);
        },
        transformed(kAttribute, kLegacyTransform, float4(0.f)));
  }

  SECTION("outTransform takes precedence over 'transform'")
  {
    checkSamplerMatches(
        d,
        [](anari::Device d, anari::Sampler s) {
          anari::setParameter(d, s, "transform", kLegacyTransform);
          anari::setParameter(d, s, "outTransform", kOutTransform);
        },
        transformed(kAttribute, kOutTransform, float4(0.f)));
  }

  anari::release(d, d);
  anari::unloadLibrary(lib);
}
