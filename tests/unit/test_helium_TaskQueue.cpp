// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"

#include "helium/TaskQueue.h"

// std
#include <atomic>
#include <functional>
#include <future>
#include <numeric>
#include <vector>

namespace {

using helium::tasking::Future;
using helium::tasking::TaskQueue;
using helium::tasking::wait;

// All tasks run on a single worker thread, one at a time, so recording into a
// shared container from within tasks needs no additional synchronization.

TEST_CASE("TaskQueue preserves FIFO order for cross-thread enqueues",
    "[helium_TaskQueue]")
{
  TaskQueue q(4);

  std::vector<int> order;
  for (int i = 0; i < 32; ++i)
    q.enqueue([&order, i]() { order.push_back(i); });

  q.flush();

  std::vector<int> expected(32);
  std::iota(expected.begin(), expected.end(), 0);
  REQUIRE(order == expected);
}

TEST_CASE("TaskQueue enqueue always appends (FIFO) instead of running inline",
    "[helium_TaskQueue]")
{
  TaskQueue q(4);

  // Written only by tasks (all on the single worker thread, in sequence).
  std::vector<char> order;
  std::promise<void> aStarted;
  std::promise<void> cEnqueued;
  std::promise<Future> bEnqueued; // A publishes B's future to the main thread

  // Task A: records its start, waits for the main thread to enqueue C, then
  // enqueues B *from the worker thread* before returning.
  q.enqueue([&]() {
    order.push_back('A'); // A start
    aStarted.set_value();
    cEnqueued.get_future().wait();
    Future fb = q.enqueue([&]() { order.push_back('B'); });
    order.push_back('a'); // A end -- proves B has not run inline yet
    bEnqueued.set_value(std::move(fb));
  });

  // Ensure A is running, then enqueue C from the main thread. C is fully
  // appended before we release A (via cEnqueued), so FIFO puts C ahead of B
  // and no append races the worker.
  aStarted.get_future().wait();
  Future fc = q.enqueue([&]() { order.push_back('C'); });
  cEnqueued.set_value();

  Future fb = bEnqueued.get_future().get();
  wait(fc);
  wait(fb);

  // A runs to completion (A then a) with B never nested inside it; C was queued
  // ahead of B, so the drain order is A, a, C, B.
  REQUIRE(order == std::vector<char>{'A', 'a', 'C', 'B'});
}

TEST_CASE("TaskQueue self-re-post does not deadlock or overflow the stack",
    "[helium_TaskQueue]")
{
  TaskQueue q(2); // deliberately small initial hint; storage grows on demand

  const int N = 5000;
  std::atomic<int> counter{0};
  std::promise<void> done;

  std::function<void()> selfPost = [&]() {
    if (++counter < N)
      q.enqueue(selfPost); // re-enqueue own continuation from the worker
    else
      done.set_value();
  };

  q.enqueue(selfPost);

  // A bounded self-driving loop completes without deadlock; because tasks are
  // appended (not nested), stack depth stays constant across all N iterations.
  done.get_future().wait();
  REQUIRE(counter.load() == N);

  // flush() afterward still returns.
  q.flush();
  SUCCEED();
}

TEST_CASE("TaskQueue drains queued tasks on shutdown", "[helium_TaskQueue]")
{
  std::atomic<int> ran{0};
  {
    TaskQueue q(4);
    for (int i = 0; i < 64; ++i)
      q.enqueue([&ran]() { ++ran; });
    // Destroy without flushing: the dtor must drain outstanding work, not hang.
  }
  REQUIRE(ran.load() == 64);
}

TEST_CASE("TaskQueue shutdown drains a bounded self-re-posting task in flight",
    "[helium_TaskQueue]")
{
  std::atomic<int> counter{0};
  const int N = 100;
  {
    // Declare the continuation before the queue so it outlives the queue's
    // dtor drain (destruction is reverse order: q first, then selfPost).
    std::function<void()> selfPost;
    TaskQueue q(2);
    selfPost = [&]() {
      if (++counter < N)
        q.enqueue(selfPost);
    };
    q.enqueue(selfPost);
    // Destroy while the bounded self-loop is still in flight -- no hang, no UB.
  }
  REQUIRE(counter.load() == N);
}

TEST_CASE("TaskQueue::onWorkerThread reflects the execution context",
    "[helium_TaskQueue]")
{
  TaskQueue q(2);

  REQUIRE_FALSE(q.onWorkerThread());

  std::atomic<bool> onWorker{false};
  q.enqueue([&]() { onWorker = q.onWorkerThread(); }).wait();
  REQUIRE(onWorker.load());
}

} // namespace
