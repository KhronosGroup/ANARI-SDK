// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Checks that built-in Tests' parameter axes actually change the ground truth
// helide generates: a permutation the reference device ignores would score
// every candidate against the same image, so it exercises nothing.

#include "catch.hpp"
// cts
#include "cts/BuiltinTests.h"
#include "cts/Catalog.h"
#include "cts/Expansion.h"
#include "cts/Image.h"
#include "cts/Runner.h"
#include "cts/Workdir.h"
// std
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace anari::cts;

namespace {

void statusFunc(const void *,
    anari::Device,
    anari::Object,
    anari::DataType,
    anari::StatusSeverity,
    anari::StatusCode,
    const char *)
{}

const TestDef *findTest(const Catalog &catalog, const std::string &id)
{
  for (const auto &t : catalog.tests()) {
    if (t.id() == id)
      return &t;
  }
  return nullptr;
}

const CaseValue *findValue(const Case &c, const std::string &axis)
{
  for (const auto &v : c.values) {
    if (v.axisName == axis)
      return &v;
  }
  return nullptr;
}

// Generates helide's ground truth for every Case of 'test' and loads each
// Case's color image. Returns false (with a warning) when helide is missing.
bool generateTruths(const Catalog &catalog,
    const TestDef &test,
    const std::vector<Case> &cases,
    std::map<const Case *, Image> &truths)
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (!lib) {
    WARN("helide library not available; skipping ground-truth comparison");
    return false;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  const auto root =
      std::filesystem::temp_directory_path() / "cts_builtin_truths_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  RunOptions opts;
  opts.width = 64;
  opts.height = 64;
  opts.device = {"helide", "default", "default"};
  Runner runner(d, Workdir(root), opts);

  const std::set<std::string> features(
      test.requiredFeatures.begin(), test.requiredFeatures.end());
  const auto summary = runner.generate(catalog, Filter{test.id()}, features);
  REQUIRE(summary.total == int(cases.size()));
  REQUIRE(summary.passed == summary.total);

  const Workdir wd(root);
  for (const auto &c : cases) {
    auto img = loadPNG(wd.groundTruthImagePath(c, Channel::Color).string());
    REQUIRE(img.valid());
    truths[&c] = std::move(img);
  }

  anari::release(d, d);
  anari::unloadLibrary(lib);
  std::filesystem::remove_all(root, ec);
  return true;
}

} // namespace

TEST_CASE("sampler/primitive inOffset changes the ground truth",
    "[cts][builtin][helide]")
{
  Catalog catalog;
  registerBuiltinTests(catalog);
  const TestDef *test = findTest(catalog, "sampler/primitive");
  REQUIRE(test != nullptr);

  // The spec parameter is 'inOffset', ANARI_UINT64. Pair each array width's
  // offset Case with its no-offset Case.
  const auto cases = expand(*test);
  std::map<std::string, std::pair<const Case *, const Case *>> byDim;
  for (const auto &c : cases) {
    const CaseValue *offset = findValue(c, "inOffset");
    const CaseValue *dim = findValue(c, "arrayDim");
    REQUIRE(offset != nullptr);
    REQUIRE(dim != nullptr);
    auto &pair = byDim[dim->value.getString()];
    if (offset->value.valid()) {
      CHECK(offset->value.type() == ANARI_UINT64);
      pair.first = &c;
    } else {
      pair.second = &c;
    }
  }
  REQUIRE_FALSE(byDim.empty());

  std::map<const Case *, Image> truths;
  if (!generateTruths(catalog, *test, cases, truths))
    return;

  for (const auto &[dim, pair] : byDim) {
    INFO("arrayDim=" << dim);
    REQUIRE(pair.first != nullptr);
    REQUIRE(pair.second != nullptr);
    CHECK(truths.at(pair.first).rgba != truths.at(pair.second).rgba);
  }
}

TEST_CASE(
    "sampler/transform outTransform and outOffset change the ground truth",
    "[cts][builtin][helide]")
{
  Catalog catalog;
  registerBuiltinTests(catalog);
  const TestDef *test = findTest(catalog, "sampler/transform");
  REQUIRE(test != nullptr);

  // The spec parameters are 'outTransform' (FLOAT32_MAT4) and 'outOffset'
  // (FLOAT32_VEC4). Index each Case by whether it sets each one.
  const auto cases = expand(*test);
  std::map<std::pair<bool, bool>, const Case *> byAxes;
  for (const auto &c : cases) {
    CHECK(findValue(c, "transform") == nullptr);
    const CaseValue *xfm = findValue(c, "outTransform");
    const CaseValue *offset = findValue(c, "outOffset");
    REQUIRE(xfm != nullptr);
    REQUIRE(offset != nullptr);
    if (xfm->value.valid())
      CHECK(xfm->value.type() == ANARI_FLOAT32_MAT4);
    if (offset->value.valid())
      CHECK(offset->value.type() == ANARI_FLOAT32_VEC4);
    byAxes[{xfm->value.valid(), offset->value.valid()}] = &c;
  }
  REQUIRE(byAxes.size() == 4);

  std::map<const Case *, Image> truths;
  if (!generateTruths(catalog, *test, cases, truths))
    return;

  // Key is {outTransform set, outOffset set}; each axis must change the truth
  // whichever value the other axis has.
  for (bool otherAxisSet : {false, true}) {
    INFO("other axis set=" << otherAxisSet);
    CHECK(truths.at(byAxes.at({true, otherAxisSet})).rgba
        != truths.at(byAxes.at({false, otherAxisSet})).rgba);
    CHECK(truths.at(byAxes.at({otherAxisSet, true})).rgba
        != truths.at(byAxes.at({otherAxisSet, false})).rgba);
  }
}
