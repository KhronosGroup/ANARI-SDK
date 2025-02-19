// Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

#include "Logging.h"

//-------------------------------------------------------------------------------------------------
// Terminal colors
//

#define TERMINAL_RED "\033[0;31m"
#define TERMINAL_GREEN "\033[0;32m"
#define TERMINAL_YELLOW "\033[1;33m"
#define TERMINAL_BLUE "\033[0;34m"
#define TERMINAL_RESET "\033[0m"
#define TERMINAL_DEFAULT TERMINAL_RESET
#define TERMINAL_BOLD "\033[1;1m"

inline std::string tolower(std::string str)
{
  std::transform(str.begin(), str.end(), str.begin(), [](char c) {
    return std::tolower(c);
  });
  return str;
}

namespace remote {
namespace logging {
void Initialize(Level maxLevel)
{
  char *logLevel = getenv("ANARI_REMOTE_LOG_LEVEL");
  if (logLevel) {
    if (tolower(std::string(logLevel)) == "error")
      OutputLevelMax = Level::Error;
    else if (tolower(std::string(logLevel)) == "warning")
      OutputLevelMax = Level::Warning;
    else if (tolower(std::string(logLevel)) == "stats")
      OutputLevelMax = Level::Stats;
    else if (tolower(std::string(logLevel)) == "info")
      OutputLevelMax = Level::Info;
  } else {
    OutputLevelMax = maxLevel;
  }
}

Stream::Stream(Level level) : level_(level) {}

Stream::~Stream()
{
  if (Level::Info <= OutputLevelMax && level_ == Level::Info)
    std::cout << TERMINAL_GREEN << stream_.str() << TERMINAL_RESET << '\n';

  if (Level::Warning <= OutputLevelMax && level_ == Level::Warning)
    std::cout << TERMINAL_YELLOW << stream_.str() << TERMINAL_RESET << '\n';

  if (Level::Stats <= OutputLevelMax && level_ == Level::Stats)
    std::cout << TERMINAL_BLUE << stream_.str() << TERMINAL_RESET << '\n';

  if (Level::Error <= OutputLevelMax && level_ == Level::Error)
    std::cout << TERMINAL_RED << stream_.str() << TERMINAL_RESET << '\n';
}
} // namespace logging
} // namespace remote
