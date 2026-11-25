// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// A flush finalizes every observer notified by an object it finalized, all
// the way down an observer chain, rather than leaving each link for a later
// flush.

#include "catch.hpp"

#include "helium/BaseGlobalDeviceState.h"
#include "helium/BaseObject.h"
#include "helium/utility/ChangeObserverPtr.h"

#include <string>
#include <vector>

namespace {

using namespace helium;

using Trace = std::vector<std::string>;

struct Traced : BaseObject
{
  Traced(ANARIDataType t, BaseGlobalDeviceState *s, Trace &trace, const char *n)
      : BaseObject(t, s), m_trace(trace), m_name(n)
  {}
  bool isValid() const override
  {
    return true;
  }
  bool getProperty(const std::string_view &,
      ANARIDataType,
      void *,
      uint64_t,
      uint32_t) override
  {
    return false;
  }
  void commitParameters() override {}
  void finalize() override
  {
    m_trace.push_back(m_name);
  }

  // markParameterChanged() is protected on BaseObject
  void stageCommit()
  {
    markParameterChanged();
    deviceState()->commitBuffer.addObjectToCommit(this);
  }

  Trace &m_trace;
  const char *m_name;
};

struct Observer : Traced
{
  Observer(BaseGlobalDeviceState *s, Trace &t, const char *n, BaseObject *o)
      : Traced(ANARI_GEOMETRY, s, t, n), m_observed(this, o)
  {}
  ChangeObserverPtr<BaseObject> m_observed;
};

} // namespace

SCENARIO("a flush finalizes an observer chain of the object it finalized",
    "[helium_commit_buffer]")
{
  BaseGlobalDeviceState state{nullptr};
  Trace trace;
  auto *observed = new Traced(ANARI_SPATIAL_FIELD, &state, trace, "observed");
  auto *first = new Observer(&state, trace, "first", observed);
  auto *second = new Observer(&state, trace, "second", first);

  GIVEN("the observed object is committed")
  {
    observed->stageCommit();
    state.commitBuffer.flush();
    THEN("one flush finalizes the whole chain and leaves nothing staged")
    {
      CHECK(trace == Trace{"observed", "first", "second"});
      CHECK(state.commitBuffer.empty());
    }
  }

  GIVEN("the observed object is queued for finalization only")
  {
    observed->markUpdated();
    state.commitBuffer.addObjectToFinalize(observed);
    state.commitBuffer.flush();
    THEN("one flush finalizes the whole chain and leaves nothing staged")
    {
      CHECK(trace == Trace{"observed", "first", "second"});
      CHECK(state.commitBuffer.empty());
    }
  }

  state.commitBuffer.clear();
  second->refDec();
  first->refDec();
  observed->refDec();
}
