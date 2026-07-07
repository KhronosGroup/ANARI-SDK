// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Device-agnostic renderer parameter overrides. The CLI collects NAME=VALUE
// pairs (e.g. ambientSample=4) and the runner applies them to every Case's
// renderer. Typing is resolved against the device itself: the runner asks the
// renderer subtype for the parameter's declared ANARIDataType and parses the
// text into it, so no device-specific knowledge lives here. When the device
// does not report the parameter, the type is inferred from the text.
//
// Parsing is pure (no device) so it is unit-testable; the runner owns the
// device introspection and the anariSetParameter call.

// anari
#include "anari/anari.h"
// std
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace anari {
namespace cts {

// A renderer parameter override supplied on the command line as NAME=VALUE.
struct RendererParam
{
  std::string name;
  std::string value; // raw, unparsed text
};

// Parse a "name=value" argument, splitting on the first '='. Returns nullopt
// when '=' is absent or the name is empty. The value may be empty and may
// itself contain '='.
std::optional<RendererParam> parseRendererParam(const std::string &arg);

// A value parsed into the byte layout anariSetParameter expects. `type` is
// ANARI_UNKNOWN only when parsing failed.
struct ParsedParam
{
  ANARIDataType type{ANARI_UNKNOWN};
  // Scalar/vector payload in native ANARI layout, or a NUL-terminated string.
  std::vector<std::byte> bytes;

  const void *data() const
  {
    return bytes.data();
  }
};

// Infer an ANARI type from `text`: bool (true/false), int32, float32,
// float32_vecN (N in 2..4, comma-separated), else string.
ANARIDataType inferParamType(const std::string &text);

// Parse `text` as `type`. When `type` is ANARI_UNKNOWN the type is inferred
// first. Returns nullopt when the text does not parse as the resolved type or
// the type is unsupported. Supported: BOOL, INT32, UINT32, FLOAT32,
// FLOAT32_VEC2/3/4, STRING.
std::optional<ParsedParam> parseParamValue(
    ANARIDataType type, const std::string &text);

} // namespace cts
} // namespace anari
