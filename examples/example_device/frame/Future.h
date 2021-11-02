// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Object.h"
// std
#include <chrono>
#include <future>
#include <memory>

namespace anari {
namespace example_device {

struct Future
{
  Future();
  ~Future() = default;

  void wait() const;
  bool isReady() const;
  void markComplete();

 private:
  std::promise<void> m_promise;
  std::future<void> m_future;
};

using FuturePtr = std::shared_ptr<Future>;

// Inlined definitions ////////////////////////////////////////////////////////

inline Future::Future()
{
  m_future = m_promise.get_future();
}

inline void Future::wait() const
{
  if (m_future.valid())
    m_future.wait();
}

inline bool Future::isReady() const
{
  return m_future.wait_for(std::chrono::seconds(0))
      == std::future_status::ready;
}

inline void Future::markComplete()
{
  m_promise.set_value();
}

} // namespace example_device
} // namespace anari