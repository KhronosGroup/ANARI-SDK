// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace anari {
namespace introspection {

// Detailed metadata copied from anariGetParameterInfo(). Typed scalar and
// vector values are stored in their canonical display form without decoration.
struct ParameterMetadata
{
  bool required{false};
  std::optional<std::string> defaultValue;
  std::optional<std::string> minimum;
  std::optional<std::string> maximum;
  std::optional<std::string> use;
  std::vector<std::string> stringValues;
  std::vector<ANARIDataType> dataTypeValues;
  std::vector<ANARIDataType> elementTypes;
  std::optional<std::string> description;
  std::optional<std::string> sourceExtension;
};

struct ParameterInfo
{
  std::string name;
  ANARIDataType type{ANARI_UNKNOWN};
  ParameterMetadata metadata;
};

struct ObjectInfo
{
  ANARIDataType type{ANARI_UNKNOWN};
  // Empty for object types without named subtypes.
  std::string subtype;
  std::vector<ParameterInfo> parameters;
};

struct ObjectTypeInfo
{
  ANARIDataType type{ANARI_UNKNOWN};
  std::vector<std::string> subtypes;
};

struct DeviceInfo
{
  bool parametersIncluded{true};
  std::vector<ObjectTypeInfo> objectTypes;
  std::vector<ObjectInfo> objects;
  std::vector<std::string> frameChannels;
};

struct QueryOptions
{
  // Filters are case-sensitive substrings of ANARI type and subtype names.
  std::string typeFilter;
  std::string subtypeFilter;
  bool includeParameters{true};
  bool includeParameterInfo{false};
};

struct FormatOptions
{
  // Presentation controls preserve the established styles of the two CLIs.
  std::string indent;
  std::size_t parameterNameWidth{32};
  std::size_t parameterTypeWidth{0};
  bool includeSdkVersion{true};
  bool includeFrameChannels{false};
  bool quoteStringValues{false};
};

DeviceInfo queryDeviceInfo(
    ANARIDevice device, const QueryOptions &options = {});

// Format a value returned by anariGetParameterInfo() according to its ANARI
// type. Unsupported types produce an empty string.
std::string formatParameterValue(ANARIDataType type, const void *value);

std::string formatDeviceInfo(
    const DeviceInfo &info, const FormatOptions &options = {});

} // namespace introspection
} // namespace anari
