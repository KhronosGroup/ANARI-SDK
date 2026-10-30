// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/BaseObject.h"
// std
#include <map>
#include <string>
#include <vector>

namespace capture_device {

struct WriterOptions
{
  // Arrays with more elements than this are written as blobs
  uint64_t inlineArrayLimit{64};
  // The extensions the device advertises
  std::vector<std::string> extensions;
};

struct Capture
{
  // The canonical text
  std::string text;
  // The blobs it refers to, by name (a content hash)
  std::map<std::string, std::vector<uint8_t>> blobs;
  // Spec violations and API misuse seen on the way, as they are in the text
  std::vector<std::string> warnings;
};

// Walks the graph reachable from 'frame', as its objects last committed it,
// and writes it
Capture writeCapture(const helium::BaseObject &frame, const WriterOptions &o);

} // namespace capture_device
