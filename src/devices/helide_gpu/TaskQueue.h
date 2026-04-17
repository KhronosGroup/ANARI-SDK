// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace helide_gpu::tasking {

using Future = std::future<void>;

/*
 * Single-worker task queue that dispatches callable tasks onto a dedicated
 * background thread; each enqueued task returns a Future for completion
 * tracking. Flushing the queue will block the caller until all tasks are
 * complete.
 *
 * Example:
 *   TaskQueue q(64);
 *   Future f = q.enqueue([]{ doWork(); });
 *   wait(f);
 */
struct TaskQueue
{
  TaskQueue(size_t n);
  ~TaskQueue();

  template <class F, class... Args>
  Future enqueue(F &&f, Args &&...args);

  void flush();

 private:
  static void thread_fun(TaskQueue *ct);

  std::vector<std::packaged_task<void()>> m_tasks;
  bool m_stop{false};
  int m_next{0};
  int m_last{0};

  std::mutex m_mutex;
  std::condition_variable m_condition;
  std::thread m_thread;
};

bool isReady(const Future &f);
void wait(const Future &f);

// Inlined definitions ////////////////////////////////////////////////////////

inline TaskQueue::TaskQueue(size_t n) : m_tasks(n), m_thread(thread_fun, this)
{}

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

  if (std::this_thread::get_id() == m_thread.get_id())
    task();
  else {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_condition.wait(
        lock, [&] { return (m_last + 1) % m_tasks.size() != m_next; });
    m_tasks[m_last] = std::move(task);
    m_last = (m_last + 1) % m_tasks.size();
    m_condition.notify_one();
  }

  return future;
}

inline void TaskQueue::flush()
{
  enqueue([]() {}).wait();
}

inline void TaskQueue::thread_fun(TaskQueue *ct)
{
  while (true) {
    std::packaged_task<void()> task;
    {
      std::unique_lock<std::mutex> lock(ct->m_mutex);
      ct->m_condition.wait(
          lock, [ct] { return ct->m_stop || ct->m_next != ct->m_last; });
      if (ct->m_stop && ct->m_next == ct->m_last)
        break;
      task = std::move(ct->m_tasks[ct->m_next]);
      ct->m_next = (ct->m_next + 1) % ct->m_tasks.size();
      ct->m_condition.notify_one();
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

} // namespace helide_gpu::tasking
