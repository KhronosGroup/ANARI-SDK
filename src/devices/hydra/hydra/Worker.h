// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The device worker thread. One long-lived thread per device owns the Hgi/GL
// context, all world-mirror mutation, every HdEngine::Execute call, and all
// Hydra-adjacent destruction. App-facing calls that need any of these post jobs
// here; the one-Hydra-thread and no-mutation-during-SyncAll rules then hold by
// construction.

#pragma once

#include "GLContext.h"

#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/driver.h>
#include <pxr/imaging/hd/rendererPlugin.h>
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
  // Starts the thread, which makes a GL context current per `mode` (see
  // GLContext.h) and, if it got one, creates the Hgi.
  explicit Worker(GLMode mode);
  ~Worker();

  // The GL context the worker made: "EGL (...)", "GLX (...)" or "none".
  const std::string &glDescription() const
  {
    return m_glDescription;
  }
  // Why no context was made (empty if one was, or for GLMode::None).
  const std::string &glError() const
  {
    return m_glError;
  }

  // Can delegate `delegateId` run on this worker? Asks the renderer plugin with
  // the worker's resources (GPU enabled iff a GL context and a supported Hgi
  // exist), so a GL-only delegate is refused without a context. Without a
  // context the plugin is never handed a GPU flag, so it never creates an
  // HgiGL against no current context. `reason` is set when unsupported.
  bool delegateSupported(const pxr::TfToken &delegateId, std::string *reason);

  // Post a job to the worker and return immediately.
  void post(std::function<void()> job);

  // Post a job and block until it completes (used for map/property paths and
  // teardown that must observe worker-side results).
  void postWait(std::function<void()> job);

  // Accessors below are only valid to call from within a worker job.
  // hgi() is null when the worker has no GL context.
  pxr::Hgi *hgi() const
  {
    return m_hgi.get();
  }
  pxr::HdDriver *driver()
  {
    return &m_driver;
  }
  // GPU resources (a GL context and a supported Hgi) are available; Hydra's
  // GPU tasks need them.
  bool gpuEnabled() const
  {
    return m_gpuEnabled;
  }

  // Is the calling thread the worker thread? (guards re-entrancy)
  bool onWorkerThread() const;

 private:
  void threadMain();

  GLMode m_glMode;
  // A GL context, current on the worker thread, that HgiGL (Storm) requires.
  std::unique_ptr<GLContext> m_glContext;
  std::string m_glDescription;
  std::string m_glError;
  pxr::HgiUniquePtr m_hgi; // created and owned on the worker thread
  bool m_gpuEnabled{false};
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
