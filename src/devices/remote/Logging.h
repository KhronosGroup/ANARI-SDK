// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <ostream>
#include <sstream>

namespace remote {
namespace logging {
enum class Level
{
  Error,
  Warning,
  Stats,
  Info,
};

static Level OutputLevelMax = Level::Warning;

void Initialize(Level maxLevel = Level::Warning);

class Stream
{
 public:
  Stream(Level level);
  ~Stream();

  inline std::ostream &stream()
  {
    return stream_;
  }

 private:
  std::ostringstream stream_;
  Level level_;
};
} // namespace logging
} // namespace remote

#define LOG(LEVEL) ::remote::logging::Stream(LEVEL).stream()
