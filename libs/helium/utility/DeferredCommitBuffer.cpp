// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "DeferredCommitBuffer.h"
#include "BaseObject.h"
// std
#include <algorithm>

namespace helium {

DeferredCommitBuffer::DeferredCommitBuffer()
{
  m_commitBuffer.reserve(100);
}

DeferredCommitBuffer::~DeferredCommitBuffer()
{
  clear();
}

void DeferredCommitBuffer::addObject(BaseObject *obj)
{
  obj->refInc(RefType::INTERNAL);
  if (commitPriority(obj->type()) != commitPriority(ANARI_OBJECT))
    m_needToSortCommits = true;
  m_commitBuffer.push_back(obj);
}

bool DeferredCommitBuffer::flush()
{
  if (m_commitBuffer.empty())
    return false;

  if (m_needToSortCommits) {
    std::sort(m_commitBuffer.begin(),
        m_commitBuffer.end(),
        [](BaseObject *o1, BaseObject *o2) {
          return commitPriority(o1->type()) < commitPriority(o2->type());
        });
  }

  m_needToSortCommits = false;

  size_t i = 0;
  size_t end = m_commitBuffer.size();
  while (i != end) {
    for (;i < end; i++) {
      auto obj = m_commitBuffer[i];
      if (obj->useCount() > 1 && obj->lastUpdated() > obj->lastCommitted()) {
        obj->commit();
        obj->markCommitted();
      }
    }
    end = m_commitBuffer.size();
  }

  clear();
  m_lastFlush = newTimeStamp();
  return true;
}

TimeStamp DeferredCommitBuffer::lastFlush() const
{
  return m_lastFlush;
}

void DeferredCommitBuffer::clear()
{
  for (auto &obj : m_commitBuffer)
    obj->refDec(RefType::INTERNAL);
  m_commitBuffer.clear();
  m_lastFlush = 0;
}

bool DeferredCommitBuffer::empty() const
{
  return m_commitBuffer.empty();
}

} // namespace helium