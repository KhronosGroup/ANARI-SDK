// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Timer.h"

#include <chrono>
#include <ratio>

namespace anari_cat {
struct Timer::Impl
{
  std::chrono::time_point<std::chrono::steady_clock> startTime;
};

Timer::Timer() : m_impl(new Timer::Impl{}) {}

Timer::~Timer() = default;

void Timer::start()
{
  m_impl->startTime = std::chrono::steady_clock::now();
}

float Timer::secondsElapsed() const
{
  return std::chrono::duration<float>(
      std::chrono::steady_clock::now() - m_impl->startTime)
      .count();
}

float Timer::millisecondsElapsed() const
{
  return std::chrono::duration<float, std::milli>(
      std::chrono::steady_clock::now() - m_impl->startTime)
      .count();
}

} // namespace anari_cat
