// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <utility>

namespace helium::tasking {

using Future = std::future<void>;

/*
 * Single-worker task queue that dispatches callable tasks onto a dedicated
 * background thread; each enqueued task returns a Future for completion
 * tracking. Tasks always run in FIFO order -- including tasks enqueued from
 * within a running task (i.e. from the worker thread itself), which are
 * appended and run after the current task returns rather than nested inline.
 * This lets a task re-enqueue its own continuation without unbounded stack
 * growth. Flushing the queue will block the caller until all tasks are
 * complete.
 *
 * Storage grows on demand: enqueue never blocks on a full queue, so a task
 * running on the worker thread can always append (a self-enqueue can never
 * deadlock waiting for a drain only it could perform). Note this removes the
 * bounded-queue backpressure the class once had; a runaway external producer
 * can grow memory without bound. The device usage here has bounded producers.
 *
 * Example:
 *   TaskQueue q(64);
 *   Future f = q.enqueue([]{ doWork(); });
 *   wait(f);
 */
struct TaskQueue
{
  // n is retained for API compatibility as an initial-size hint; storage now
  // grows on demand so it imposes no upper bound.
  TaskQueue(size_t n);
  ~TaskQueue();

  template <class F, class... Args>
  Future enqueue(F &&f, Args &&...args);

  void flush();
  bool onWorkerThread() const;

 private:
  static void thread_fun(TaskQueue *ct);

  std::deque<std::packaged_task<void()>> m_tasks;
  bool m_stop{false};

  std::mutex m_mutex;
  std::condition_variable m_condition;
  std::thread m_thread;
};

bool isReady(const Future &f);
void wait(const Future &f);

// Inlined definitions ////////////////////////////////////////////////////////

inline TaskQueue::TaskQueue(size_t /*n*/) : m_thread(thread_fun, this) {}

inline TaskQueue::~TaskQueue()
{
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_stop = true;
    m_condition.notify_all();
  }
  m_thread.join();
}

template <class F, class... Args>
inline Future TaskQueue::enqueue(F &&f, Args &&...args)
{
  std::packaged_task<void()> task(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...));
  Future future = task.get_future();

  // Always append (FIFO), never run inline. Growable storage means we never
  // wait on a "not full" predicate, so a task enqueuing from the worker thread
  // cannot block on a drain only it could perform.
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_tasks.push_back(std::move(task));
    m_condition.notify_one();
  }

  return future;
}

inline void TaskQueue::flush()
{
  enqueue([]() {}).wait();
}

inline bool TaskQueue::onWorkerThread() const
{
  return std::this_thread::get_id() == m_thread.get_id();
}

inline void TaskQueue::thread_fun(TaskQueue *ct)
{
  while (true) {
    std::packaged_task<void()> task;
    {
      std::unique_lock<std::mutex> lock(ct->m_mutex);
      ct->m_condition.wait(
          lock, [ct] { return ct->m_stop || !ct->m_tasks.empty(); });
      // Drain any remaining tasks before stopping (drain-or-stop).
      if (ct->m_stop && ct->m_tasks.empty())
        break;
      task = std::move(ct->m_tasks.front());
      ct->m_tasks.pop_front();
    }
    task();
  }
}

inline bool isReady(const Future &f)
{
  return !f.valid()
      || f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

inline void wait(const Future &f)
{
  if (f.valid())
    f.wait();
}

} // namespace helium::tasking
