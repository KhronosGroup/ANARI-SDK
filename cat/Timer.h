// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include <memory>

namespace anari_cat {

class Timer
{
  struct Impl;
  std::unique_ptr<Impl> m_impl;

 public:
  Timer();
  Timer(const Timer &) = delete;
  Timer(Timer &&) = delete;
  Timer &operator=(const Timer &) = delete;
  Timer &operator=(Timer &&) = delete;
  ~Timer();

  void start();

  float secondsElapsed() const;
  float millisecondsElapsed() const;
};

} // namespace anari_cat