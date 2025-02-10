// Copyright 2022-2024 The Khronos Group
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
  addObjectToCommitImpl(obj);
}

void DeferredCommitBuffer::addObjectToFinalize(BaseObject *obj)
{
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  addObjectToFinalizeImpl(obj);
}

bool DeferredCommitBuffer::flush()
{
  if (empty())
    return false;

  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  flushCommits();
  const bool didFinalize = flushFinalizations();

  clear();
  m_lastFlush = newTimeStamp();
  return didFinalize;
}

TimeStamp DeferredCommitBuffer::lastFlush() const
{
  return m_lastFlush;
}

void DeferredCommitBuffer::clear()
{
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  for (auto &obj : m_commitBuffer)
    obj->refDec(RefType::INTERNAL);
  for (auto &obj : m_finalizationBuffer)
    obj->refDec(RefType::INTERNAL);
  m_commitBuffer.clear();
  m_finalizationBuffer.clear();
  m_lastFlush = 0;
}

bool DeferredCommitBuffer::empty() const
{
  return m_commitBuffer.empty() && m_finalizationBuffer.empty();
}

void DeferredCommitBuffer::addObjectToCommitImpl(BaseObject *obj)
{
  obj->refInc(RefType::INTERNAL);
  m_commitBuffer.push_back(obj);
}

void DeferredCommitBuffer::addObjectToFinalizeImpl(BaseObject *obj)
{
  obj->refInc(RefType::INTERNAL);
  if (commitPriority(obj->type()) != commitPriority(ANARI_OBJECT))
    m_needToSortFinalizations = true;
  m_finalizationBuffer.push_back(obj);
}

void DeferredCommitBuffer::flushCommits()
{
  dynamic_foreach(m_commitBuffer, [&](size_t i) {
    auto obj = m_commitBuffer[i];
    if (obj->lastParameterChanged() > obj->lastCommitted()) {
      obj->commitParameters();
      obj->markCommitted();
      obj->markUpdated();
      addObjectToFinalizeImpl(obj);
    }
  });
}

bool DeferredCommitBuffer::flushFinalizations()
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
    }
  });

  return didFinalize;
}

} // namespace helium