// Copyright 2023-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace helide {

// Keeps renders from reading app arrays while the app has any mapped: a render
// (frameStart()) waits for every mapped array to be unmapped, and mapping an
// array waits for a render in flight to end.
//
// So a thread holding a mapped array can't wait for a queued render, nor for
// work queued behind one: the render can't start until that thread unmaps.
// Each mapping records its thread, and renders are counted in the order they
// are queued (helide's single worker starts them in that order), so callers
// can check for that case and report it instead of waiting.
struct RenderingSemaphore
{
  RenderingSemaphore() = default;

  void arrayMapAcquire(const void *array);
  void arrayMapRelease(const void *array);

  // Queues a render with 'enqueue', which must only queue (not run or wait
  // on) work that calls frameStart() before anything else, then frameEnd().
  // Returns the render's number for whyThisThreadCantWaitFor().
  template <typename F>
  uint64_t queueRender(F &&enqueue);
  void frameStart();
  void frameEnd();

  // Queues device work with 'enqueue' (as queueRender() does) unless a render
  // queued before it waits for an array this thread has mapped, so waiting on
  // the work would deadlock; then returns why (for "... would deadlock: <why>;
  // not waiting"), without queueing. nullptr if it queued the work.
  template <typename F>
  const char *queueUnlessThisThreadBlocksIt(F &&enqueue);

  // Why this thread can't wait for render number 'render' (it has an array
  // mapped the render waits for), or nullptr if it can.
  const char *whyThisThreadCantWaitFor(uint64_t render);

  // Waits until a queued render is waiting for mapped arrays in frameStart(),
  // so the worker runs nothing else until they are unmapped. Only call this
  // when whyThisThreadCantWaitFor() a queued render isn't nullptr.
  void waitForRenderBlockedOnMaps();

 private:
  const char *whyThisThreadCantWaitForImpl(uint64_t render) const;

  std::mutex m_mutex;
  std::condition_variable m_conditionArrays;
  std::condition_variable m_conditionFrame;
  std::condition_variable m_conditionRenderBlocked;
  // mapped arrays and the threads that mapped them (an array mapped twice is
  // listed twice)
  std::unordered_multimap<const void *, std::thread::id> m_mappedArrays;
  uint64_t m_rendersQueued{0};
  uint64_t m_rendersStarted{0};
  bool m_renderBlockedOnMaps{false};
  bool m_frameInFlight{false};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline void RenderingSemaphore::arrayMapAcquire(const void *array)
{
  std::unique_lock<std::mutex> frameLock(m_mutex);
  m_conditionFrame.wait(frameLock, [&]() { return !m_frameInFlight; });
  m_mappedArrays.emplace(array, std::this_thread::get_id());
}

inline void RenderingSemaphore::arrayMapRelease(const void *array)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  // Unmapping an array this thread mapped twice, or one another thread
  // mapped, releases one of its mappings (this thread's first).
  auto range = m_mappedArrays.equal_range(array);
  auto mapping = range.first;
  for (auto m = range.first; m != range.second; ++m) {
    if (m->second == std::this_thread::get_id()) {
      mapping = m;
      break;
    }
  }
  if (mapping == range.second)
    return;
  m_mappedArrays.erase(mapping);
  if (m_mappedArrays.empty())
    m_conditionArrays.notify_one();
}

template <typename F>
inline uint64_t RenderingSemaphore::queueRender(F &&enqueue)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  enqueue();
  return ++m_rendersQueued;
}

inline void RenderingSemaphore::frameStart()
{
  std::unique_lock<std::mutex> arraysLock(m_mutex);
  if (!m_mappedArrays.empty()) {
    m_renderBlockedOnMaps = true;
    m_conditionRenderBlocked.notify_all();
    m_conditionArrays.wait(
        arraysLock, [&]() { return m_mappedArrays.empty(); });
    m_renderBlockedOnMaps = false;
  }
  m_rendersStarted++;
  m_frameInFlight = true;
}

inline void RenderingSemaphore::frameEnd()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_frameInFlight = false;
  m_conditionFrame.notify_all();
}

template <typename F>
inline const char *RenderingSemaphore::queueUnlessThisThreadBlocksIt(
    F &&enqueue)
{
  // Held while queueing, so no render is queued between the check and the
  // work (queueRender() holds it too).
  std::lock_guard<std::mutex> lock(m_mutex);
  if (const char *why = whyThisThreadCantWaitForImpl(m_rendersQueued))
    return why;
  enqueue();
  return nullptr;
}

inline const char *RenderingSemaphore::whyThisThreadCantWaitFor(uint64_t render)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return whyThisThreadCantWaitForImpl(render);
}

inline void RenderingSemaphore::waitForRenderBlockedOnMaps()
{
  std::unique_lock<std::mutex> lock(m_mutex);
  m_conditionRenderBlocked.wait(lock, [&]() { return m_renderBlockedOnMaps; });
}

inline const char *RenderingSemaphore::whyThisThreadCantWaitForImpl(
    uint64_t render) const
{
  // Renders start in the order they were queued. One not yet started waits
  // for every mapped array, including this thread's.
  if (render <= m_rendersStarted)
    return nullptr;
  for (auto &m : m_mappedArrays) {
    if (m.second == std::this_thread::get_id()) {
      return "this thread has an array mapped, which a queued render waits "
             "to be unmapped (unmap arrays first)";
    }
  }
  return nullptr;
}

} // namespace helide
