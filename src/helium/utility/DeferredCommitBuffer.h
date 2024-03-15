// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "TimeStamp.h"
// std
#include <vector>

namespace helium {

struct BaseObject;

struct DeferredCommitBuffer
{
  DeferredCommitBuffer();
  ~DeferredCommitBuffer();

  // Add an object to this buffer. Object ref counts are incremented by 1 while
  // objects are in this buffer.
  void addObject(BaseObject *obj);

  // Sort objects by priority and call BaseObject::commit() on each object
  bool flush();

  // Return when this buffer was last flushed
  TimeStamp lastFlush() const;

  // Clear the buffer without committing any of them
  void clear();

  // Return if the buffer is empty or not
  bool empty() const;

 private:
  std::vector<BaseObject *> m_commitBuffer;
  bool m_needToSortCommits{false};
  TimeStamp m_lastFlush{0};
};

} // namespace helium