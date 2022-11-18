// Copyright 2022 The Khronos Group
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

  void addObject(BaseObject *obj);

  bool flush();
  TimeStamp lastFlush() const;

  void clear();
  bool empty() const;

 private:
  std::vector<BaseObject *> m_commitBuffer;
  bool m_needToSortCommits{false};
  TimeStamp m_lastFlush{0};
};

} // namespace helium