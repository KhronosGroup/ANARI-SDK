// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
// anari
#include "anari/anari_cpp.hpp"
#include "anari/frontend/anari_device_introspection.hpp"
// std
#include <cctype>
#include <string>

namespace introspection = anari::introspection;

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

std::string normalizedParameters(const std::string &report)
{
  const auto begin = report.find("Parameters:");
  if (begin == std::string::npos)
    return {};

  std::string normalized;
  for (auto i = begin; i < report.size(); ++i) {
    const unsigned char c = report[i];
    if (!std::isspace(c) && c != '"')
      normalized += static_cast<char>(c);
  }
  return normalized;
}

} // namespace

TEST_CASE("device introspection reports named subtypes and parameters",
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

  const auto info = introspection::queryDeviceInfo(d);
  const std::string out = introspection::formatDeviceInfo(info);
  CHECK(has(out, "SDK version:"));
  CHECK(has(out, "Subtypes:"));
  CHECK(has(out, "Parameters:"));
  CHECK(has(out, "perspective"));
  CHECK(has(out, "triangle"));
  CHECK(has(out, "ANARI_CAMERA perspective:"));

  introspection::QueryOptions detailedOptions;
  detailedOptions.typeFilter = "ANARI_CAMERA";
  detailedOptions.subtypeFilter = "perspective";
  detailedOptions.includeParameterInfo = true;
  const auto detailedInfo = introspection::queryDeviceInfo(d, detailedOptions);
  const std::string detailed = introspection::formatDeviceInfo(detailedInfo);
  CHECK(has(detailed, "default = 0.0, 0.0, -1.0"));
  CHECK(has(detailed, "default = 1.0"));
  CHECK(has(detailed, "use = point"));
  CHECK(has(detailed, "description = \"main viewing direction\""));
  CHECK(has(detailed, "sourceExtension = KHR_CAMERA_PERSPECTIVE"));

  introspection::FormatOptions anariInfoFormat;
  anariInfoFormat.indent = "   ";
  anariInfoFormat.parameterNameWidth = 25;
  anariInfoFormat.parameterTypeWidth = 25;
  anariInfoFormat.includeSdkVersion = false;
  anariInfoFormat.includeFrameChannels = true;
  anariInfoFormat.quoteStringValues = true;
  const std::string anariInfoReport =
      introspection::formatDeviceInfo(detailedInfo, anariInfoFormat);
  CHECK(has(anariInfoReport, "use = \"point\""));
  CHECK(has(detailed, "use = point"));
  CHECK(
      normalizedParameters(anariInfoReport) == normalizedParameters(detailed));

  introspection::QueryOptions geometryOptions;
  geometryOptions.typeFilter = "ANARI_GEOMETRY";
  geometryOptions.subtypeFilter = "cone";
  geometryOptions.includeParameterInfo = true;
  const std::string geometry = introspection::formatDeviceInfo(
      introspection::queryDeviceInfo(d, geometryOptions));
  CHECK(has(geometry, "default = none"));
  CHECK(has(geometry, "value =\n               \"none\""));
  CHECK(has(geometry, "elementType =\n               ANARI_FLOAT32_VEC3"));
  CHECK(has(geometry, "required"));

  introspection::QueryOptions frameOptions;
  frameOptions.typeFilter = "ANARI_FRAME";
  frameOptions.includeParameterInfo = true;
  const std::string frame = introspection::formatDeviceInfo(
      introspection::queryDeviceInfo(d, frameOptions));
  CHECK(has(frame, "ANARI_FRAME:"));
  CHECK(has(frame, "value =\n               ANARI_UFIXED8_VEC4"));
  CHECK(has(frame, "sourceExtension = KHR_FRAME_CHANNEL_PRIMITIVE_ID"));

  introspection::QueryOptions rendererOptions;
  rendererOptions.typeFilter = "ANARI_RENDERER";
  rendererOptions.subtypeFilter = "default";
  rendererOptions.includeParameterInfo = true;
  const std::string renderer = introspection::formatDeviceInfo(
      introspection::queryDeviceInfo(d, rendererOptions));
  CHECK(has(renderer, "default = 4"));
  CHECK(has(renderer, "minimum = 1"));
  CHECK(has(renderer, "maximum = 128"));

  introspection::QueryOptions filteredOptions;
  filteredOptions.typeFilter = "ANARI_CAMERA";
  filteredOptions.subtypeFilter = "perspective";
  const std::string filtered = introspection::formatDeviceInfo(
      introspection::queryDeviceInfo(d, filteredOptions));
  CHECK(has(filtered, "ANARI_CAMERA perspective:"));
  CHECK_FALSE(has(filtered, "ANARI_CAMERA orthographic:"));
  CHECK_FALSE(has(filtered, "ANARI_GEOMETRY:"));
  CHECK_FALSE(has(filtered, "ANARI_GEOMETRY triangle:"));
  CHECK_FALSE(has(filtered, "ANARI_FRAME:"));

  introspection::QueryOptions subtypeOnlyOptions;
  subtypeOnlyOptions.includeParameters = false;
  const std::string subtypeOnly = introspection::formatDeviceInfo(
      introspection::queryDeviceInfo(d, subtypeOnlyOptions));
  CHECK(has(subtypeOnly, "Subtypes:"));
  CHECK_FALSE(has(subtypeOnly, "Parameters:"));

  anari::release(d, d);
  anari::unloadLibrary(lib);
}
