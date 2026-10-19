// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace halcyon {

// Keeps the device from reading app arrays while the app has any mapped
// (KHR_DEVICE_SYNCHRONIZATION): device work (a render, a commit buffer flush,
// an ANARI_WAIT property query) waits for every mapped array to be unmapped,
// and mapping an array waits for device work in progress to end. Device work
// runs on the single task queue worker, so at most one piece of it is in
// progress at a time.
//
// A thread holding a mapped array can't wait on device work (frameReady or
// getProperty with ANARI_WAIT, anariMapFrame): the work can't start until the
// array is unmapped. Callers check whyThisThreadCantWait() and report an error
// instead of waiting.
//
// A release that privatizes a shared array holds device work off the same way
// while helium copies it (a release hold), so a status callback raised during
// the copy can't wait on device work either.
//
// Callers keep the record in step with the arrays' mapped state: HalcyonDevice
// makes each map, unmap and release-while-mapped one step with respect to the
// others, so racing threads can't leave a mapped array unrecorded. It doesn't
// wait for device work inside that step: a status callback during a render may
// unmap or release arrays.
struct RenderingSemaphore
{
  // Holds on device work, each identified by a key: a mapped 'array' (which
  // may be unmapped from another thread), or a release hold's key.
  // Acquiring fails while device work is in progress; wait for it to end with
  // waitForDeviceWork() (holding no lock the device work could need) and retry.
  bool tryArrayMapAcquire(const void *array);
  void arrayMapRelease(const void *array);
  bool tryBeginReleaseHold(const void *key);
  void endReleaseHold(const void *key);
  void waitForDeviceWork();

  // If the calling thread holds device work off (a mapped array or a release
  // hold) and so can't wait on it, why, for an error message ("... would
  // deadlock: <reason>; not waiting"); nullptr if it can wait
  const char *whyThisThreadCantWait();

  // Device work in progress from construction until end() or destruction.
  // Nested device work on the same thread (e.g. a status callback issuing an
  // ANARI_WAIT query during a render) joins the outer one.
  struct DeviceWork
  {
    DeviceWork(RenderingSemaphore &s);
    ~DeviceWork();
    void end();

    // True if this thread is already inside device work
    static bool active();

    DeviceWork(const DeviceWork &) = delete;
    DeviceWork &operator=(const DeviceWork &) = delete;

   private:
    RenderingSemaphore *m_semaphore{nullptr};
    static thread_local bool t_active;
  };

 private:
  enum class HoldKind
  {
    ARRAY_MAP,
    RELEASE
  };

  struct Hold
  {
    std::thread::id thread;
    HoldKind kind;
  };

  bool tryHoldAcquire(const void *key, HoldKind kind);
  void holdRelease(const void *key);
  void frameStart();
  void frameEnd();

  std::mutex m_mutex;
  std::condition_variable m_holdsReleased;
  std::condition_variable m_frameEnded;
  std::unordered_map<const void *, Hold> m_holds;
  bool m_frameInFlight{false};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline bool RenderingSemaphore::tryArrayMapAcquire(const void *array)
{
  return tryHoldAcquire(array, HoldKind::ARRAY_MAP);
}

inline void RenderingSemaphore::arrayMapRelease(const void *array)
{
  holdRelease(array);
}

inline bool RenderingSemaphore::tryBeginReleaseHold(const void *key)
{
  return tryHoldAcquire(key, HoldKind::RELEASE);
}

inline void RenderingSemaphore::endReleaseHold(const void *key)
{
  holdRelease(key);
}

inline void RenderingSemaphore::waitForDeviceWork()
{
  std::unique_lock<std::mutex> lock(m_mutex);
  m_frameEnded.wait(lock, [&]() { return !m_frameInFlight; });
}

inline const char *RenderingSemaphore::whyThisThreadCantWait()
{
  const auto self = std::this_thread::get_id();
  std::lock_guard<std::mutex> lock(m_mutex);
  bool releasing = false;
  for (auto &h : m_holds) {
    if (h.second.thread != self)
      continue;
    // a mapped array is the cause the app can act on
    if (h.second.kind == HoldKind::ARRAY_MAP)
      return "this thread has an array mapped (unmap arrays first)";
    releasing = true;
  }
  return releasing ? "this thread is releasing an array (don't wait on the "
                     "device from a status callback during anariRelease())"
                   : nullptr;
}

inline bool RenderingSemaphore::tryHoldAcquire(const void *key, HoldKind kind)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_frameInFlight)
    return false;
  m_holds[key] = {std::this_thread::get_id(), kind};
  return true;
}

inline void RenderingSemaphore::holdRelease(const void *key)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_holds.erase(key) != 0 && m_holds.empty())
    m_holdsReleased.notify_all();
}

inline void RenderingSemaphore::frameStart()
{
  std::unique_lock<std::mutex> lock(m_mutex);
  m_holdsReleased.wait(lock, [&]() { return m_holds.empty(); });
  m_frameInFlight = true;
}

inline void RenderingSemaphore::frameEnd()
{
  // unlike helide, the flag is written under the mutex
  std::lock_guard<std::mutex> lock(m_mutex);
  m_frameInFlight = false;
  m_frameEnded.notify_all();
}

inline thread_local bool RenderingSemaphore::DeviceWork::t_active = false;

inline RenderingSemaphore::DeviceWork::DeviceWork(RenderingSemaphore &s)
{
  if (t_active)
    return;
  s.frameStart();
  m_semaphore = &s;
  t_active = true;
}

inline RenderingSemaphore::DeviceWork::~DeviceWork()
{
  end();
}

inline void RenderingSemaphore::DeviceWork::end()
{
  if (!m_semaphore)
    return;
  m_semaphore->frameEnd();
  m_semaphore = nullptr;
  t_active = false;
}

inline bool RenderingSemaphore::DeviceWork::active()
{
  return t_active;
}

} // namespace halcyon
