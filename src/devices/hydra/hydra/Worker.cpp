// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Worker.h"

#include <pxr/imaging/hd/rendererPluginHandle.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/imaging/hgi/tokens.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace anari_hydra {

Worker::Worker(GLMode mode) : m_glMode(mode)
{
  m_thread = std::thread([this]() { threadMain(); });

  // Wait until the worker has made its GL context and Hgi, so callers can rely
  // on glDescription() and hgi() once the constructor returns.
  std::unique_lock<std::mutex> lock(m_mutex);
  m_startedCv.wait(lock, [this]() { return m_started; });
}

Worker::~Worker()
{
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_running = false;
  }
  m_cv.notify_all();
  if (m_thread.joinable())
    m_thread.join();
}

void Worker::threadMain()
{
  // Make a GL context current on this thread before creating the Hgi; HgiGL
  // (Storm) binds to whatever context is current at creation. Without one,
  // create no Hgi at all: HgiGL against no context is unusable, and Garch
  // loads its GL entry points once per process, at the first HgiGL.
  m_glContext = GLContext::create(m_glMode, m_glDescription, m_glError);

  // The Hgi lives entirely on this thread.
  if (m_glContext) {
    m_hgi = Hgi::CreatePlatformDefaultHgi();
    m_gpuEnabled = m_hgi && m_hgi->IsBackendSupported();
    m_driver = HdDriver{HgiTokens->renderDriver, VtValue(m_hgi.get())};
  }

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_threadId = std::this_thread::get_id();
    m_started = true;
  }
  m_startedCv.notify_all();

  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_cv.wait(lock, [this]() { return !m_jobs.empty() || !m_running; });
      if (m_jobs.empty() && !m_running)
        break;
      job = std::move(m_jobs.front());
      m_jobs.pop();
    }
    job();
  }

  // Drain any jobs enqueued during shutdown so destruction jobs still run.
  for (;;) {
    std::function<void()> job;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_jobs.empty())
        break;
      job = std::move(m_jobs.front());
      m_jobs.pop();
    }
    job();
  }

  m_hgi.reset();
  m_glContext.reset();
}

void Worker::post(std::function<void()> job)
{
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_jobs.push(std::move(job));
  }
  m_cv.notify_one();
}

void Worker::postWait(std::function<void()> job)
{
  // Re-entrant: if we are already on the worker thread, run inline to avoid
  // self-deadlock (self-re-posting converge poll, teardown from a worker job).
  if (onWorkerThread()) {
    job();
    return;
  }

  std::promise<void> done;
  std::future<void> fut = done.get_future();
  post([&job, &done]() {
    job();
    done.set_value();
  });
  fut.wait();
}

bool Worker::delegateSupported(const TfToken &delegateId, std::string *reason)
{
  bool supported = false;
  postWait([&]() {
    HdRendererPluginHandle plugin =
        HdRendererPluginRegistry::GetInstance().GetOrCreateRendererPlugin(
            delegateId);
    if (!plugin) {
      if (reason)
        *reason = "no Hydra renderer plugin '" + delegateId.GetString() + "'";
      return;
    }
    HdRendererCreateArgs args;
    args.gpuEnabled = m_gpuEnabled;
    args.hgi = m_hgi.get();
    std::string why;
    supported = plugin->IsSupported(args, &why);
    if (!supported && reason)
      *reason = why.empty() ? "unsupported" : why;
  });
  return supported;
}

bool Worker::onWorkerThread() const
{
  return std::this_thread::get_id() == m_threadId;
}

} // namespace anari_hydra
