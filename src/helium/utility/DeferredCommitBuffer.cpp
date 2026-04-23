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
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  obj->refInc(RefType::INTERNAL);
  m_commitBufferStaging.push_back(obj);
}

void DeferredCommitBuffer::addObjectToFinalize(BaseObject *obj)
{
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  obj->refInc(RefType::INTERNAL);
  if (commitPriority(obj->type()) != commitPriority(ANARI_OBJECT))
    m_needToSortFinalizationsStaging = true;
  m_finalizationBufferStaging.push_back(obj);
}

void DeferredCommitBuffer::flush()
{
  if (empty())
    return;
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
  return m_commitBufferStaging.empty() && m_finalizationBufferStaging.empty();
}

void DeferredCommitBuffer::swapBuffers()
{
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
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
      obj->commitParameters();
      obj->markCommitted();
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
    auto obj = m_finalizationBuffer[i];
    if (obj->lastUpdated() > obj->lastFinalized()) {
      didFinalize = true;
      obj->finalize();
      obj->markFinalized();
      obj->notifyChangeObservers();
    }
  });

  if (didFinalize)
    m_lastFinalization = newTimeStamp();
}

void DeferredCommitBuffer::clearImpl()
{
  for (auto &obj : m_commitBuffer)
    obj->refDec(RefType::INTERNAL);
  for (auto &obj : m_finalizationBuffer)
    obj->refDec(RefType::INTERNAL);
  m_commitBuffer.clear();
  m_finalizationBuffer.clear();
  m_needToSortFinalizations = false;
}

} // namespace helium