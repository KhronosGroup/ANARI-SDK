// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// An object getProperty() with ANARI_WAIT flushes the commit buffer and then
// queries the object. BaseDevice runs both through runDeviceWork(), so a
// device that flushes and renders on a worker thread can run them there
// instead of on the app thread, where they could overlap a render.

#include "catch.hpp"
#include "helium_test_device.h"

#include "helium/BaseFrame.h"
#include "helium/TaskQueue.h"

#include <chrono>
#include <future>
#include <thread>

namespace {

using namespace std::chrono_literals;
using helium_test::TestDevice;

// Records the thread that commits it and the thread that answers its "value"
// query, which returns the committed "value" parameter.
struct Probe : public helium::BaseObject
{
  Probe(helium::BaseGlobalDeviceState *s) : BaseObject(ANARI_GEOMETRY, s) {}

  bool isValid() const override
  {
    return true;
  }
  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *mem,
      uint64_t,
      uint32_t) override
  {
    if (name != "value" || type != ANARI_INT32)
      return false;
    queryThread = std::this_thread::get_id();
    *(int *)mem = committedValue;
    return true;
  }
  void commitParameters() override
  {
    commitThread = std::this_thread::get_id();
    committedValue = getParam<int>("value", 0);
  }
  void finalize() override {}

  int committedValue{0};
  std::thread::id commitThread;
  std::thread::id queryThread;
};

// A frame that answers a "duration" query and does nothing else.
struct IdleFrame : public helium::BaseFrame
{
  IdleFrame(helium::BaseGlobalDeviceState *s) : BaseFrame(s) {}

  bool isValid() const override
  {
    return true;
  }
  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *mem,
      uint64_t,
      uint32_t) override
  {
    if (name != "duration" || type != ANARI_FLOAT32)
      return false;
    *(float *)mem = 1.f;
    return true;
  }
  void commitParameters() override {}
  void finalize() override {}
  void renderFrame() override {}
  void *map(std::string_view, uint32_t *, uint32_t *, ANARIDataType *) override
  {
    return nullptr;
  }
  void unmap(std::string_view) override {}
  int frameReady(ANARIWaitMask) override
  {
    return 1;
  }
  void discard() override {}
};

// Runs device work on a worker thread, as a device that renders on a task
// queue does (directly when already on the worker).
struct WorkerDevice : public TestDevice
{
  ~WorkerDevice() override
  {
    queue.flush();
    state()->commitBuffer.clear();
  }

  void runDeviceWork(const std::function<void()> &work) override
  {
    if (queue.onWorkerThread())
      work();
    else
      queue.enqueue(work).wait();
  }

  std::thread::id workerThread()
  {
    std::thread::id id;
    queue.enqueue([&] { id = std::this_thread::get_id(); }).wait();
    return id;
  }

  helium::tasking::TaskQueue queue{4};
};

// Sets 'probe's "value" to 'v' and commits it through the device.
void setAndCommit(helium::BaseDevice &d, Probe *probe, int v)
{
  auto o = (ANARIObject)probe;
  d.setParameter(o, "value", ANARI_INT32, &v);
  d.commitParameters(o);
}

int query(helium::BaseDevice &d, Probe *probe, uint32_t mask)
{
  int v = -1;
  d.getProperty((ANARIObject)probe, "value", ANARI_INT32, &v, sizeof(v), mask);
  return v;
}

} // namespace

SCENARIO("an object ANARI_WAIT query flushes and queries as device work",
    "[helium_wait_query]")
{
  GIVEN("a device that runs device work on a worker thread")
  {
    auto *device = new WorkerDevice;
    auto *probe = new Probe(device->state());
    const auto worker = device->workerThread();
    setAndCommit(*device, probe, 1);

    THEN("an ANARI_WAIT query commits and answers on the worker")
    {
      CHECK(query(*device, probe, ANARI_WAIT) == 1);
      CHECK(probe->commitThread == worker);
      CHECK(probe->queryThread == worker);
    }

    THEN("an ANARI_NO_WAIT query answers on the caller without flushing")
    {
      CHECK(query(*device, probe, ANARI_NO_WAIT) == 0);
      CHECK(probe->queryThread == std::this_thread::get_id());
    }

    probe->refDec(helium::RefType::PUBLIC);
    delete device;
  }

  GIVEN("a device that keeps the default device work")
  {
    auto *device = new TestDevice;
    auto *probe = new Probe(device->state());
    setAndCommit(*device, probe, 2);

    THEN("an ANARI_WAIT query commits and answers on the caller")
    {
      CHECK(query(*device, probe, ANARI_WAIT) == 2);
      CHECK(probe->commitThread == std::this_thread::get_id());
      CHECK(probe->queryThread == std::this_thread::get_id());
    }

    device->state()->commitBuffer.clear();
    probe->refDec(helium::RefType::PUBLIC);
    delete device;
  }
}

SCENARIO("a frame query run as device work on a worker skips the frame lock",
    "[helium_wait_query]")
{
  // Leaked on purpose: if the query deadlocks, its threads still use them.
  auto *device = new WorkerDevice;
  auto *frame = new IdleFrame(device->state());
  auto f = (ANARIFrame)frame;

  GIVEN("an app thread holding the frame's lock, as frameReady(WAIT) does")
  {
    // An app thread waiting on a frame holds its lock until the frame's work
    // (queued behind this query on the worker) finishes.
    auto frameLock = frame->scopeLockObject();

    THEN("an ANARI_WAIT query on the frame does not wait for that lock")
    {
      std::promise<float> result;
      auto duration = result.get_future();
      std::thread([device, f, p = std::move(result)]() mutable {
        float d = 0.f;
        device->getProperty(
            f, "duration", ANARI_FLOAT32, &d, sizeof(d), ANARI_WAIT);
        p.set_value(d);
      }).detach();

      REQUIRE(duration.wait_for(5s) == std::future_status::ready);
      CHECK(duration.get() == 1.f);

      frameLock.unlock();
      frame->refDec(helium::RefType::PUBLIC);
      delete device;
    }
  }
}
