// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Case.h"
#include "Channel.h"
// anari
#include "anari/anari_cpp.hpp"
// std
#include <string>

namespace anari {
namespace cts {

// Resolving the ANARIDataType a Case renders a channel at. The color, albedo,
// and normal channels carry an output-format axis (the legacy
// frame_<channel>_type values) so a Test can exercise each buffer format; every
// other channel uses a fixed default. Pulled out of the runner so the mapping
// is unit-testable without a device (helide can't render the albedo/normal
// channels at all).
//
// This is the format *selection*; the matching validated readback lives in
// FrameReadback.cpp, which dispatches on the device-reported pixel type. A new
// format string here needs a corresponding supported type and conversion there.

// The fixed default output format for a channel's frame buffer.
ANARIDataType channelDefaultFormat(Channel channel);

// Map an output-format axis string to its ANARIDataType, or ANARI_UNKNOWN for
// an unrecognized string so the caller can fall back to the channel default.
ANARIDataType colorFormatFromString(
    const std::string &s);
ANARIDataType albedoFormatFromString(
    const std::string &s);
ANARIDataType normalFormatFromString(
    const std::string &s);

// The output format a Case requests for a channel: its output-format axis value
// (frame_color_type / frame_albedo_type / frame_normal_type) when present and
// recognized, else the channel's fixed default.
ANARIDataType caseChannelFormat(
    const Case &c, Channel channel);

} // namespace cts
} // namespace anari
