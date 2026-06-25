// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
// cts
#include "cts/DeviceInfo.h"
// anari
#include "anari/anari_cpp.hpp"
// std
#include <string>

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

bool has(const std::string &haystack, const std::string &needle)
{
  return haystack.find(needle) != std::string::npos;
}

} // namespace

TEST_CASE("queryDeviceInfo introspects device subtypes and parameters",
    "[cts][deviceinfo][helide]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (!lib) {
    WARN("helide library not available; skipping device-info test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  SECTION("full report names subtypes and a parameters section")
  {
    const std::string out = queryDeviceInfo(d);
    CHECK(has(out, "SDK version:"));
    CHECK(has(out, "Subtypes:"));
    CHECK(has(out, "Parameters:"));
    // Subtypes every reference device advertises.
    CHECK(has(out, "perspective")); // camera
    CHECK(has(out, "triangle")); // geometry
    // A subtype's parameter is listed with its ANARI type.
    CHECK(has(out, "CAMERA perspective:"));
  }

  SECTION("skipParameters lists subtypes but omits the parameters section")
  {
    const std::string out = queryDeviceInfo(d, "", "", /*skipParameters*/ true);
    CHECK(has(out, "Subtypes:"));
    CHECK(has(out, "perspective"));
    CHECK_FALSE(has(out, "Parameters:"));
  }

  SECTION("type filter restricts output to the matching object type")
  {
    const std::string out = queryDeviceInfo(d, "CAMERA");
    CHECK(has(out, "CAMERA"));
    CHECK(has(out, "perspective"));
    // The parameters section should not list other object types' subtypes.
    CHECK_FALSE(has(out, "GEOMETRY triangle:"));
  }

  SECTION("info mode is a superset of the plain listing")
  {
    const std::string plain = queryDeviceInfo(d);
    const std::string detailed = queryDeviceInfo(d, "", "", false, true);
    // Detailed output keeps every line the plain listing has (parameters are
    // still named) and only adds per-parameter info, so it is at least as long.
    CHECK(detailed.size() >= plain.size());
    CHECK(has(detailed, "CAMERA perspective:"));
  }

  anari::release(d, d);
  anari::unloadLibrary(lib);
}
