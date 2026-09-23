// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Regressions for updates that arrive on an app thread while the commit buffer
// flushes on another thread (a device that flushes on a worker, or an app that
// edits from a different thread than the one rendering). Each test holds the
// flush inside an object's commitParameters()/finalize() with a gate, lands
// the concurrent update, then checks that the next flush still applies it.

#include "catch.hpp"

#include "helium/BaseObject.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace {

using namespace std::chrono_literals;

// Lets a test hold a flush at a known point: the flushing thread calls
// arriveAndWait(), the test waits for arrived() and then calls release().
struct Gate
{
  void arriveAndWait()
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_arrived = true;
    m_cv.notify_all();
    m_cv.wait(lock, [&] { return m_released; });
  }

  void waitForArrival()
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [&] { return m_arrived; });
  }

  void release()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_released = true;
    m_cv.notify_all();
  }

 private:
  std::mutex m_mutex;
  std::condition_variable m_cv;
  bool m_arrived{false};
  bool m_released{false};
};

// Stands in for an observed array: edit() writes its data and notifies
// observers, as helium::Array::unmap() does.
struct Source : public helium::BaseObject
{
  Source(helium::BaseGlobalDeviceState *s) : BaseObject(ANARI_OBJECT, s) {}
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
  void finalize() override {}

  void edit(int v)
  {
    data = v;
    notifyChangeObservers();
  }

  std::atomic<int> data{0};
};

// Reads its "value" parameter at commit and its source's data at finalize.
struct Observer : public helium::BaseObject
{
  Observer(helium::BaseGlobalDeviceState *s) : BaseObject(ANARI_GEOMETRY, s) {}
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

  void commitParameters() override
  {
    committedValue = getParam<int>("value", 0);
    if (onCommit)
      onCommit();
  }

  void finalize() override
  {
    if (source)
      finalizedData = source->data.load();
    if (onFinalize)
      onFinalize();
  }

  void markCommitted() override
  {
    if (onMarkCommitted)
      onMarkCommitted();
    BaseObject::markCommitted();
  }

  Source *source{nullptr};
  std::atomic<int> committedValue{0};
  std::atomic<int> finalizedData{0};
  std::function<void()> onCommit;
  std::function<void()> onFinalize;
  std::function<void()> onMarkCommitted;
};

// What BaseDevice::setParameter() + commitParameters() do for an object.
void commit(helium::BaseGlobalDeviceState &state, Observer *o, int value)
{
  o->setParam("value", value);
  o->markParameterChanged();
  o->snapshotParameters();
  state.commitBuffer.addObjectToCommit(o);
}

} // namespace

SCENARIO("an observer notified during its finalize() is finalized again",
    "[helium_concurrent_updates]")
{
  helium::BaseGlobalDeviceState state(nullptr);
  auto *source = new Source(&state);
  auto *observer = new Observer(&state);
  observer->source = source;
  source->addChangeObserver(observer);

  source->edit(1);
  state.commitBuffer.flush();
  REQUIRE(observer->finalizedData == 1);

  GIVEN("an edit that lands while the flush is inside finalize()")
  {
    Gate gate;
    observer->onFinalize = [&] { gate.arriveAndWait(); };
    source->edit(2);
    std::thread flusher([&] { state.commitBuffer.flush(); });
    gate.waitForArrival(); // finalize() has read 2 and is still running
    source->edit(3);
    gate.release();
    flusher.join();
    observer->onFinalize = nullptr;
    REQUIRE(observer->finalizedData == 2);

    THEN("the next flush finalizes the object with the later edit")
    {
      state.commitBuffer.flush();
      REQUIRE(observer->finalizedData == 3);
    }
  }

  source->removeChangeObserver(observer);
  state.commitBuffer.clear();
  observer->refDec(helium::RefType::PUBLIC);
  source->refDec(helium::RefType::PUBLIC);
}

SCENARIO("a commit issued while the flush commits the same object is kept",
    "[helium_concurrent_updates]")
{
  helium::BaseGlobalDeviceState state(nullptr);
  auto *observer = new Observer(&state);

  commit(state, observer, 1);
  state.commitBuffer.flush();
  REQUIRE(observer->committedValue == 1);

  GIVEN("a re-commit that blocks on the snapshot while commitParameters() runs")
  {
    Gate gate;
    std::atomic<bool> recommitted{false};
    observer->onCommit = [&] { gate.arriveAndWait(); };
    // Widen the window between commitParameters() and markCommitted(): wait
    // (briefly) for the concurrent re-commit to publish its snapshot. If the
    // flush still holds the snapshot mutex here, it cannot, and this times out.
    observer->onMarkCommitted = [&] {
      const auto deadline = std::chrono::steady_clock::now() + 200ms;
      while (!recommitted && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    };

    commit(state, observer, 2);
    std::thread flusher([&] { state.commitBuffer.flush(); });
    gate.waitForArrival(); // commitParameters() has read 2
    std::thread app([&] {
      commit(state, observer, 3); // blocks on the snapshot mutex
      recommitted = true;
    });
    std::this_thread::sleep_for(20ms); // let the re-commit reach the mutex
    gate.release();
    flusher.join();
    app.join();
    observer->onCommit = nullptr;
    observer->onMarkCommitted = nullptr;
    REQUIRE(observer->committedValue == 2);

    THEN("the next flush commits the re-committed value")
    {
      state.commitBuffer.flush();
      REQUIRE(observer->committedValue == 3);
    }
  }

  state.commitBuffer.clear();
  observer->refDec(helium::RefType::PUBLIC);
}
