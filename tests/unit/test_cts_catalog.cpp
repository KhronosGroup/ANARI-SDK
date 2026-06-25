// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
// cts harness
#include "cts/BuildContext.h"
#include "cts/Case.h"
#include "cts/Catalog.h"
#include "cts/Expansion.h"
#include "cts/Filter.h"
#include "cts/TestBuilder.h"
#include "cts/Value.h"
// std
#include <set>
#include <string>

using namespace anari::cts;

namespace {

// Collect the ground-truth keys / ids of every Case, in order.
std::vector<std::string> gtKeys(const std::vector<Case> &cases)
{
  std::vector<std::string> out;
  for (const auto &c : cases)
    out.push_back(c.groundTruthKey());
  return out;
}

std::vector<std::string> ids(const std::vector<Case> &cases)
{
  std::vector<std::string> out;
  for (const auto &c : cases)
    out.push_back(c.id());
  return out;
}

size_t uniqueCount(std::vector<std::string> v)
{
  std::set<std::string> s(v.begin(), v.end());
  return s.size();
}

} // namespace

// Value serialization /////////////////////////////////////////////////////////

TEST_CASE("anyToString renders axis values for keys", "[cts][value]")
{
  CHECK(anyToString(none()) == "none");
  CHECK(anyToString(Any(16)) == "16");
  CHECK(anyToString(Any(uint32_t(54321))) == "54321");
  CHECK(anyToString(Any(true)) == "true");
  CHECK(anyToString(Any(false)) == "false");
  CHECK(anyToString(Any("indexed")) == "indexed");

  SECTION("floats drop trailing zeros")
  {
    CHECK(anyToString(Any(0.6f)) == "0.6");
    CHECK(anyToString(Any(100.0f)) == "100");
    CHECK(anyToString(Any(1.77f)) == "1.77");
    CHECK(anyToString(Any(0.01f)) == "0.01");
    CHECK(anyToString(Any(1.0f)) == "1");
  }

  SECTION("path-unsafe characters are sanitized")
  {
    CHECK(anyToString(Any("a/b c")) == "a-b-c");
  }

  SECTION("vector and matrix values render component-wise and distinctly")
  {
    using anari::math::float2;
    using anari::math::float3;
    using anari::math::float4;
    // Commas are sanitized to '-' so the key stays a safe path segment.
    CHECK(anyToString(Any(float3(0.f, 0.f, 1.f))) == "0-0-1");
    CHECK(anyToString(Any(float2(0.25f, 0.75f))) == "0.25-0.75");
    CHECK(anyToString(Any(float4(0.7f, -0.4f, 0.6f, -0.2f)))
        == "0.7--0.4-0.6--0.2");
    // Distinct vectors must yield distinct keys (no ground-truth collision).
    CHECK(anyToString(Any(float3(0.f, 0.f, 1.f)))
        != anyToString(Any(float3(0.f, 0.f, -1.f))));
  }
}

// Expansion: counts
// ////////////////////////////////////////////////////////////

TEST_CASE("a Test with no axes expands to one default Case", "[cts][expand]")
{
  TestDef t;
  t.category = "geometry";
  t.name = "empty";
  auto cases = expand(t);
  REQUIRE(cases.size() == 1);
  CHECK(cases[0].id() == "default");
  CHECK(cases[0].groundTruthKey() == "default");
  CHECK(cases[0].category == "geometry");
  CHECK(cases[0].testName == "empty");
}

TEST_CASE("an axis with no values yields no Cases", "[cts][expand]")
{
  TestDef t;
  t.axes.push_back(Axis{"x", {}, AxisKind::Permutation});
  CHECK(expand(t).empty());
}

TEST_CASE("full cartesian product over all axes", "[cts][expand]")
{
  auto t = makeTest("geometry", "sphere")
               .permute("primitiveCount", {1, 16})
               .variant("primitiveMode", {"soup", "indexed"})
               .take();

  auto cases = expand(t);

  REQUIRE(cases.size() == 4); // 2 * 2

  // Last axis varies fastest.
  CHECK(ids(cases)
      == std::vector<std::string>{
          "1_soup", "1_indexed", "16_soup", "16_indexed"});
}

TEST_CASE("cartesian count is the product of axis sizes", "[cts][expand]")
{
  auto t = makeTest("c", "n")
               .permute("a", {1, 2, 3})
               .permute("b", {1, 2})
               .variant("d", {"x", "y", "z", "w"})
               .take();
  CHECK(expand(t).size() == 3 * 2 * 4);
}

// Expansion: ground-truth keys
// //////////////////////////////////////////////////

TEST_CASE("variants share a ground-truth key; permutations do not",
    "[cts][expand][groundtruth]")
{
  auto t = makeTest("geometry", "sphere")
               .permute("primitiveCount", {1, 16})
               .variant("primitiveMode", {"soup", "indexed"})
               .take();

  auto cases = expand(t);
  REQUIRE(cases.size() == 4);

  // Four distinct Cases...
  CHECK(uniqueCount(ids(cases)) == 4);
  // ...but only two ground-truth images (one per permutation value).
  CHECK(uniqueCount(gtKeys(cases)) == 2);

  // soup and indexed of the same primitiveCount share their ground truth.
  CHECK(cases[0].groundTruthKey() == cases[1].groundTruthKey()); // both "1"
  CHECK(cases[2].groundTruthKey() == cases[3].groundTruthKey()); // both "16"
  CHECK(cases[0].groundTruthKey() == "1");
  CHECK(cases[2].groundTruthKey() == "16");
}

TEST_CASE("a variant-only Test shares a single ground truth across all cases",
    "[cts][expand][groundtruth]")
{
  auto t = makeTest("geometry", "modes")
               .variant("primitiveMode", {"soup", "indexed"})
               .take();
  auto cases = expand(t);
  REQUIRE(cases.size() == 2);
  CHECK(cases[0].groundTruthKey() == "default");
  CHECK(cases[1].groundTruthKey() == "default");
  CHECK(cases[0].id() == "soup");
  CHECK(cases[1].id() == "indexed");
}

// Expansion: simplified (one-factor-at-a-time)
// //////////////////////////////////

TEST_CASE(
    "simplified expansion is one-factor-at-a-time", "[cts][expand][simplified]")
{
  // Mirrors cts/test_scenes/camera/perspective.json: 4 axes of 2 values each.
  // Full cartesian would be 16; simplified is 1 baseline + 4 = 5.
  auto t = makeTest("camera", "perspective")
               .simplified()
               .permute("fovy", {none(), Any(0.6f)})
               .permute("aspect", {none(), Any(1.77f)})
               .permute("near", {Any(0.01f), Any(1.0f)})
               .permute("far", {Any(100.0f), Any(1.0f)})
               .take();

  auto cases = expand(t);

  REQUIRE(cases.size() == 5);

  CHECK(gtKeys(cases)
      == std::vector<std::string>{
          "none_none_0.01_100", // baseline (all first values)
          "0.6_none_0.01_100", // fovy varied
          "none_1.77_0.01_100", // aspect varied
          "none_none_1_100", // near varied
          "none_none_0.01_1", // far varied
      });
}

TEST_CASE("simplified count is 1 + sum(len-1)", "[cts][expand][simplified]")
{
  auto t = makeTest("c", "n")
               .simplified()
               .permute("a", {1, 2, 3}) // +2
               .permute("b", {1, 2, 3, 4}) // +3
               .variant("d", {"x", "y"}) // +1
               .take();
  CHECK(expand(t).size() == 1 + 2 + 3 + 1);
}

TEST_CASE("simplified honors variant/permutation kinds for keys",
    "[cts][expand][simplified]")
{
  auto t = makeTest("c", "n")
               .simplified()
               .permute("count", {1, 16})
               .variant("mode", {"soup", "indexed"})
               .take();
  auto cases = expand(t);
  // baseline (1, soup), then count=16 (16, soup), then mode=indexed (1,
  // indexed)
  REQUIRE(cases.size() == 3);
  CHECK(
      ids(cases) == std::vector<std::string>{"1_soup", "16_soup", "1_indexed"});
  // The variant case shares the baseline's ground truth.
  CHECK(cases[0].groundTruthKey() == "1");
  CHECK(cases[2].groundTruthKey() == "1");
  CHECK(cases[0].groundTruthKey() == cases[2].groundTruthKey());
  CHECK(cases[1].groundTruthKey() == "16");
}

// Filter
// ////////////////////////////////////////////////////////////////////////

TEST_CASE("globMatch handles wildcards anchored over the whole string",
    "[cts][filter]")
{
  CHECK(globMatch("abc", "abc"));
  CHECK_FALSE(globMatch("abc", "abcd"));
  CHECK(globMatch("a*c", "abc"));
  CHECK(globMatch("a*c", "ac"));
  CHECK(globMatch("a*c", "aXXXc"));
  CHECK(globMatch("a?c", "abc"));
  CHECK_FALSE(globMatch("a?c", "ac"));
  CHECK(globMatch("*", "anything at all"));
  CHECK(globMatch("geometry/*", "geometry/sphere"));
  CHECK_FALSE(globMatch("geometry/*", "light/point"));
  CHECK(globMatch("*sphere", "geometry/sphere"));
  CHECK(globMatch("ABC", "abc")); // case-insensitive
}

TEST_CASE(
    "Filter selects by substring or glob over the test id", "[cts][filter]")
{
  auto t = makeTest("geometry", "sphere").take();

  CHECK(matches(Filter{""}, t)); // empty matches all
  CHECK(matches(Filter{"geometry"}, t)); // substring
  CHECK(matches(Filter{"sphere"}, t));
  CHECK(matches(Filter{"GEOMETRY"}, t)); // case-insensitive substring
  CHECK(matches(Filter{"geometry/sphere"}, t));
  CHECK_FALSE(matches(Filter{"light"}, t));

  CHECK(matches(Filter{"geometry/*"}, t)); // glob
  CHECK(matches(Filter{"*sphere"}, t));
  CHECK_FALSE(matches(Filter{"light/*"}, t));
}

// Feature gating
// /////////////////////////////////////////////////////////////////

TEST_CASE("isSupported requires every feature to be present", "[cts][features]")
{
  auto t = makeTest("geometry", "sphere")
               .requireFeatures({"ANARI_KHR_GEOMETRY_SPHERE",
                   "ANARI_KHR_CAMERA_PERSPECTIVE"})
               .take();

  CHECK(isSupported(t,
      {"ANARI_KHR_GEOMETRY_SPHERE",
          "ANARI_KHR_CAMERA_PERSPECTIVE",
          "ANARI_KHR_LIGHT_DIRECTIONAL"}));
  CHECK_FALSE(isSupported(t, {"ANARI_KHR_GEOMETRY_SPHERE"}));
  CHECK_FALSE(isSupported(t, {}));

  SECTION("a Test with no required features is always supported")
  {
    auto u = makeTest("demo", "free").take();
    CHECK(isSupported(u, {}));
  }
}

// Builder
// ///////////////////////////////////////////////////////////////////////

TEST_CASE(
    "makeTest builds a TestDef with the configured fields", "[cts][builder]")
{
  auto t = makeTest("geometry", "sphere")
               .permute("primitiveCount", {1, 16})
               .variant("primitiveMode", {"soup", "indexed"})
               .requireFeature("ANARI_KHR_GEOMETRY_SPHERE")
               .threshold("ssim", 0.8)
               .threshold("psnr", 25.0)
               .boundsTolerance(0.1)
               .channels({Channel::Color, Channel::Depth})
               .take();

  CHECK(t.id() == "geometry/sphere");
  REQUIRE(t.axes.size() == 2);
  CHECK(t.axes[0].name == "primitiveCount");
  CHECK(t.axes[0].kind == AxisKind::Permutation);
  CHECK(t.axes[1].name == "primitiveMode");
  CHECK(t.axes[1].kind == AxisKind::Variant);
  REQUIRE(t.requiredFeatures.size() == 1);
  CHECK(t.requiredFeatures[0] == "ANARI_KHR_GEOMETRY_SPHERE");
  CHECK(t.thresholdOr("ssim", 0.0) == Approx(0.8));
  CHECK(t.thresholdOr("psnr", 0.0) == Approx(25.0));
  CHECK(t.thresholdOr("missing", 0.7) == Approx(0.7)); // default
  CHECK(t.boundsTolerance == Approx(0.1));
  REQUIRE(t.channels.size() == 2);
  CHECK(t.channels[0] == Channel::Color);
  CHECK(t.channels[1] == Channel::Depth);
  CHECK_FALSE(t.simplified);
}

TEST_CASE("channels default to color only", "[cts][builder]")
{
  auto t = makeTest("c", "n").take();
  REQUIRE(t.channels.size() == 1);
  CHECK(t.channels[0] == Channel::Color);
}

// Per-channel thresholds //////////////////////////////////////////////////////

TEST_CASE("thresholdFor falls back through channel, test-wide, then default",
    "[cts][builder][threshold]")
{
  auto t = makeTest("frame", "channels")
               .threshold("ssim", 0.8) // test-wide
               .threshold(Channel::Depth, "psnr", 35.0) // per-channel override
               .threshold(Channel::Depth, "ssim", 0.95)
               .take();

  // Per-channel override wins where present.
  CHECK(t.thresholdFor(Channel::Depth, "psnr", 20.0) == Approx(35.0));
  CHECK(t.thresholdFor(Channel::Depth, "ssim", 0.7) == Approx(0.95));

  // A channel with no override falls back to the test-wide metric threshold...
  CHECK(t.thresholdFor(Channel::Color, "ssim", 0.7) == Approx(0.8));
  // ...and to the supplied default when the test sets neither.
  CHECK(t.thresholdFor(Channel::Color, "psnr", 20.0) == Approx(20.0));
  CHECK(t.thresholdFor(Channel::Normal, "ssim", 0.7) == Approx(0.8));
}

// BuildContext
// ////////////////////////////////////////////////////////////////////

TEST_CASE(
    "BuildContext round-trips axis values by name and type", "[cts][context]")
{
  BuildContext ctx; // null device is fine for value storage

  CHECK(ctx.device() == nullptr);

  ctx.set("primitiveCount", Any(5));
  CHECK(ctx.get<int>("primitiveCount", 1) == 5);
  CHECK(ctx.has("primitiveCount"));

  SECTION("missing values fall back to the default")
  {
    CHECK(ctx.get<int>("notThere", 7) == 7);
    CHECK_FALSE(ctx.has("notThere"));
  }

  SECTION("a none() value is dropped so the default is used")
  {
    ctx.set("fovy", none());
    CHECK_FALSE(ctx.has("fovy"));
    CHECK(ctx.get<float>("fovy", 0.5f) == Approx(0.5f));
  }

  SECTION("strings are read with getString")
  {
    ctx.set("primitiveMode", Any("indexed"));
    CHECK(ctx.getString("primitiveMode", "soup") == "indexed");
    CHECK(ctx.getString("missing", "soup") == "soup");
  }

  SECTION("typed setter")
  {
    ctx.setValue("radius", 2.5f);
    CHECK(ctx.get<float>("radius", 0.f) == Approx(2.5f));
  }
}

// Catalog
// //////////////////////////////////////////////////////////////////////////

TEST_CASE("Catalog registers, lists, and filters tests", "[cts][catalog]")
{
  Catalog catalog;
  makeTest("geometry", "sphere").registerInto(catalog);
  makeTest("geometry", "triangle").registerInto(catalog);
  makeTest("light", "directional").registerInto(catalog);

  CHECK(catalog.size() == 3);
  CHECK(catalog.list().size() == 3);

  SECTION("categories are sorted and de-duplicated")
  {
    CHECK(
        catalog.categories() == std::vector<std::string>{"geometry", "light"});
  }

  SECTION("filter selects a slice")
  {
    auto geo = catalog.filter(Filter{"geometry"});
    REQUIRE(geo.size() == 2);
    CHECK(geo[0]->id() == "geometry/sphere");
    CHECK(geo[1]->id() == "geometry/triangle");

    CHECK(catalog.filter(Filter{"directional"}).size() == 1);
    CHECK(catalog.filter(Filter{""}).size() == 3);
    CHECK(catalog.filter(Filter{"nomatch"}).empty());
  }
}
