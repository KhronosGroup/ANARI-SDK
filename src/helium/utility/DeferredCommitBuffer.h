// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "TimeStamp.h"
// std
#include <mutex>
#include <vector>

namespace helium {

struct BaseObject;

/*
 * Pending-commit queue that decouples anariCommitParameters() from the actual
 * object update work. When the application calls anariCommitParameters(), the
 * object is enqueued here rather than updated immediately. The device flushes
 * the buffer at frame start and before property queries. During flush, objects
 * are sorted by commit priority (Frame > World > Instance > Group > Surface/
 * Volume > Material > others) so that dependencies are always committed before
 * the objects that reference them. Each object goes through two phases: first
 * commitParameters() (re-reads parameters), then finalize() (updates state that
 * depends on other already-committed objects). TimeStamps are used to skip
 * redundant work when parameters have not changed since the last commit.
 */
struct DeferredCommitBuffer
{
  DeferredCommitBuffer();
  ~DeferredCommitBuffer();

  // Add an object to be committed. Object ref counts are incremented by 1 while
  // objects are in this buffer. This will put the object into the finalization
  // buffer if-needed (don't do this at the call site).
  void addObjectToCommit(BaseObject *obj);

  // Add an object to be finalized only.
  void addObjectToFinalize(BaseObject *obj);

  // Sort objects by priority and call BaseObject::commitParameters() and
  // BaseObject::finalize() on each object.
  void flush();

  // Return when this buffer was last committed any object
  TimeStamp lastObjectCommit() const;

  // Return when this buffer was last finalized any object
  TimeStamp lastObjectFinalization() const;

  // Clear the buffer without committing any of them
  void clear();

  // Return if the buffer is empty or not
  bool empty() const;

 private:
  void addObjectToCommitImpl(BaseObject *obj);
  void addObjectToFinalizeImpl(BaseObject *obj);
  void flushCommits();
  void flushFinalizations();
  void clearImpl();

  std::vector<BaseObject *> m_commitBuffer;
  std::vector<BaseObject *> m_finalizationBuffer;
  bool m_needToSortFinalizations{false};
  TimeStamp m_lastCommit{0};
  TimeStamp m_lastFinalization{0};
  std::recursive_mutex m_mutex;
};

} // namespace helium