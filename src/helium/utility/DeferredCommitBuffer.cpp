// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "DeferredCommitBuffer.h"
#include "BaseObject.h"
// std
#include <algorithm>

namespace helium {

// Helper functions ///////////////////////////////////////////////////////////

template <typename T, typename FCN_T>
static void dynamic_foreach(std::vector<T> &buffer, FCN_T &&fcn)
{
  size_t i = 0;
  size_t end = buffer.size();
  while (i != end) {
    for (; i < end; i++)
      fcn(i);
    end = buffer.size();
  }
}

// DeferredCommitBuffer definitions ///////////////////////////////////////////

DeferredCommitBuffer::DeferredCommitBuffer()
{
  m_commitBufferStaging.reserve(100);
  m_finalizationBufferStaging.reserve(100);
  m_commitBuffer.reserve(100);
  m_finalizationBuffer.reserve(100);
}

DeferredCommitBuffer::~DeferredCommitBuffer()
{
  clear();
}

void DeferredCommitBuffer::addObjectToCommit(BaseObject *obj)
{
  std::lock_guard<std::recursive_mutex> guard(m_swapMutex);
  obj->refInc(RefType::INTERNAL);
  m_commitBufferStaging.push_back(obj);
}

void DeferredCommitBuffer::addObjectToFinalize(BaseObject *obj)
{
  std::lock_guard<std::recursive_mutex> guard(m_swapMutex);
  obj->refInc(RefType::INTERNAL);
  if (commitPriority(obj->type()) != commitPriority(ANARI_OBJECT))
    m_needToSortFinalizationsStaging = true;
  m_finalizationBufferStaging.push_back(obj);
}

void DeferredCommitBuffer::flush()
{
  std::lock_guard<std::recursive_mutex> guard(m_flushMutex);
  // Only the thread holding m_flushMutex sees m_flushing set: this call is
  // nested in its flush (e.g. a status callback from commitParameters() or
  // finalize() issuing an ANARI_WAIT query). Swapping the buffers the outer
  // flush is walking would corrupt it; what's staged waits for the next flush.
  if (m_flushing || empty())
    return;
  m_flushing = true;
  struct FlushingScope
  {
    bool &flushing;
    ~FlushingScope()
    {
      flushing = false;
    }
  } scope{m_flushing};
  swapBuffers();
  flushCommits();
  flushFinalizations();
  clearImpl();
}

TimeStamp DeferredCommitBuffer::lastObjectCommit() const
{
  return m_lastCommit;
}

TimeStamp DeferredCommitBuffer::lastObjectFinalization() const
{
  return m_lastFinalization;
}

void DeferredCommitBuffer::clear()
{
  clearImpl();
  swapBuffers();
  clearImpl();
}

bool DeferredCommitBuffer::empty() const
{
  // m_flushMutex makes flush() wait out a flush running on another thread;
  // m_swapMutex guards the staging buffers against a concurrent add.
  std::lock_guard<std::recursive_mutex> flushGuard(m_flushMutex);
  std::lock_guard<std::recursive_mutex> swapGuard(m_swapMutex);
  return m_commitBufferStaging.empty() && m_finalizationBufferStaging.empty();
}

void DeferredCommitBuffer::swapBuffers()
{
  std::lock_guard<std::recursive_mutex> guard(m_swapMutex);
  std::swap(m_commitBuffer, m_commitBufferStaging);
  std::swap(m_finalizationBuffer, m_finalizationBufferStaging);
  std::swap(m_needToSortFinalizations, m_needToSortFinalizationsStaging);
}

void DeferredCommitBuffer::flushCommits()
{
  bool didCommit = false;
  dynamic_foreach(m_commitBuffer, [&](size_t i) {
    auto obj = m_commitBuffer[i];
    if (obj->lastParameterChanged() > obj->lastCommitted()) {
      didCommit = true;
      // Read the committed snapshot (taken at anariCommitParameters() time),
      // not the live store, so a setParam that arrived after the commit call
      // does not leak into this commit. ReadCommittedScope holds the object's
      // snapshot mutex (not its object lock -- a frame call such as a
      // device's renderFrame() may hold the object lock while blocked on this
      // flush, so that would deadlock), serializing
      // the read against a concurrent re-commit of the same object.
      //
      // markCommitted() reads the snapshot's parameter-change time, so it runs
      // inside the same scope: a concurrent re-commit blocked on the snapshot
      // mutex must not publish its newer time before this commit records the
      // one it actually read, or that re-commit would be skipped as already
      // committed.
      {
        ParameterizedObject::ReadCommittedScope readScope(obj);
        obj->commitParameters();
        obj->markCommitted();
      }
      obj->markUpdated();
      {
        obj->refInc(RefType::INTERNAL);
        if (commitPriority(obj->type()) != commitPriority(ANARI_OBJECT))
          m_needToSortFinalizations = true;
        m_finalizationBuffer.push_back(obj);
      }
    }
  });

  if (didCommit)
    m_lastCommit = newTimeStamp();
}

void DeferredCommitBuffer::flushFinalizations()
{
  if (m_needToSortFinalizations) {
    std::sort(m_finalizationBuffer.begin(),
        m_finalizationBuffer.end(),
        [](BaseObject *o1, BaseObject *o2) {
          return commitPriority(o1->type()) < commitPriority(o2->type());
        });
  }

  m_needToSortFinalizations = false;

  bool didFinalize = false;
  dynamic_foreach(m_finalizationBuffer, [&](size_t i) {
    if (m_finalizationBuffer[i]->finalizeIfUpdated())
      didFinalize = true;
  });

  if (didFinalize)
    m_lastFinalization = newTimeStamp();
}

void DeferredCommitBuffer::clearImpl()
{
  std::lock_guard<std::recursive_mutex> guard(m_swapMutex);
  for (auto &obj : m_commitBuffer)
    obj->refDec(RefType::INTERNAL);
  for (auto &obj : m_finalizationBuffer)
    obj->refDec(RefType::INTERNAL);
  m_commitBuffer.clear();
  m_finalizationBuffer.clear();
  m_needToSortFinalizations = false;
}

} // namespace helium