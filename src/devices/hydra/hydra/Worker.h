// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The device worker thread (SPEC §9.2). One long-lived thread per device owns
// the Hgi/GL context, all world-mirror mutation, every HdEngine::Execute call,
// and all Hydra-adjacent destruction. App-facing calls that need any of these
// post jobs here; the one-Hydra-thread and no-mutation-during-SyncAll rules
// then hold by construction.

#pragma once

#include <pxr/imaging/garch/glDebugWindow.h>
#include <pxr/imaging/hd/driver.h>
#include <pxr/imaging/hgi/hgi.h>

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace anari_hydra {

class Worker
{
 public:
  Worker();
  ~Worker();

  // Post a job to the worker and return immediately.
  void post(std::function<void()> job);

  // Post a job and block until it completes (used for map/property paths and
  // teardown that must observe worker-side results).
  void postWait(std::function<void()> job);

  // Accessors below are only valid to call from within a worker job.
  pxr::Hgi *hgi() const
  {
    return m_hgi.get();
  }
  pxr::HdDriver *driver()
  {
    return &m_driver;
  }

  // Is the calling thread the worker thread? (guards re-entrancy)
  bool onWorkerThread() const;

 private:
  void threadMain();

  // A GL context, current on the worker thread, that HgiGL (Storm) requires.
  // The hdx/Storm ride-along (SPEC §13) means we establish it unconditionally.
  std::unique_ptr<pxr::GarchGLDebugWindow> m_glWindow;
  pxr::HgiUniquePtr m_hgi; // created and owned on the worker thread
  pxr::HdDriver m_driver;

  std::thread m_thread;
  std::thread::id m_threadId;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::queue<std::function<void()>> m_jobs;
  bool m_running{true};
  bool m_started{false};
  std::condition_variable m_startedCv;
};

} // namespace anari_hydra
