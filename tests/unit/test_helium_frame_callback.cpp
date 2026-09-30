// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Regressions for frame completion callbacks that call back into the device,
// on the frame that is completing or on another frame, while an app thread is
// blocked waiting on (or mapping) that frame. The callback's calls must not
// wait for the frame's object lock: BaseDevice doesn't hold it while waiting
// for a frame. A helide callback that would itself have to wait for a frame
// whose render is queued behind it gets an ERROR instead.

#include "catch.hpp"
#include "helide_render_test.h"
#include "helium_test_device.h"

#include "helium/BaseDevice.h"
#include "helium/BaseFrame.h"
// anari
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

namespace {

using namespace std::chrono_literals;

// One-shot signal between threads.
struct Event
{
  void set()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_set = true;
    m_cv.notify_all();
  }

  void wait()
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [&] { return m_set; });
  }

  bool isSet()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_set;
  }

 private:
  std::mutex m_mutex;
  std::condition_variable m_cv;
  bool m_set{false};
};

// renderFrame() completes on its own thread once 'renderGate' is set (by
// default, once an app thread is blocked waiting on the frame), then invokes
// the completion callback there. Like a device's, frameReady(ANARI_WAIT) and
// map() wait for that, except from the frame's own callback. It opts in to
// waiting without its object lock.
struct TestFrame : public helium::BaseFrame
{
  TestFrame(helium::BaseGlobalDeviceState *s) : BaseFrame(s) {}

  ~TestFrame() override
  {
    if (m_worker.joinable())
      m_worker.join();
  }

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

  void renderFrame() override
  {
    m_worker = std::thread([this]() {
      (renderGate ? renderGate : &appWaiting)->wait();
      rendered = true;
      invokeCompletionCallback(callback, callbackUserPtr, device);
      done.set();
    });
  }

  void *map(std::string_view,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType) override
  {
    waitForRender();
    *width = 1;
    *height = 1;
    *pixelType = ANARI_FLOAT32;
    return &pixel;
  }

  void unmap(std::string_view) override {}

  int frameReady(ANARIWaitMask m) override
  {
    if (m == ANARI_NO_WAIT)
      return rendered;
    waitForRender();
    return 1;
  }

  void discard() override {}

  void waitWithoutObjectLock() override
  {
    waitForRender();
  }

  ANARIFrameCompletionCallback callback{nullptr};
  const void *callbackUserPtr{nullptr};
  ANARIDevice device{nullptr};
  Event *renderGate{nullptr};
  Event appWaiting;
  Event done;
  std::atomic<bool> rendered{false};
  float pixel{0.5f};

 private:
  void waitForRender()
  {
    if (completingOnThisThread())
      return;
    appWaiting.set();
    done.wait();
  }

  std::thread m_worker;
};

using helium_test::TestDevice;

// A frame that doesn't override waitWithoutObjectLock(). Its frameReady()
// with ANARI_WAIT and map() record whether its object lock is held meanwhile:
// whether another thread's setParameter() on it is still blocked after a
// while.
struct LockedWaitFrame : public helium::BaseFrame
{
  LockedWaitFrame(helium::BaseGlobalDeviceState *s, TestDevice *d)
      : BaseFrame(s), m_device(d)
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
  void finalize() override {}
  void renderFrame() override {}

  void *map(std::string_view,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType) override
  {
    probeLock();
    *width = 1;
    *height = 1;
    *pixelType = ANARI_FLOAT32;
    return &pixel;
  }

  void unmap(std::string_view) override {}

  int frameReady(ANARIWaitMask m) override
  {
    if (m == ANARI_WAIT)
      probeLock();
    return 1;
  }

  void discard() override {}

  void joinProbe()
  {
    if (m_probe.joinable())
      m_probe.join();
  }

  bool lockHeldWhileWaiting{false};
  float pixel{0.5f};

 private:
  // The probe finishes once the caller unlocks the frame; joinProbe() then.
  void probeLock()
  {
    if (m_probe.joinable())
      return;
    m_probe = std::thread([this]() {
      int value = 1;
      m_device->setParameter((ANARIObject)this, "value", ANARI_INT32, &value);
      m_probeSet = true;
    });
    std::this_thread::sleep_for(100ms);
    lockHeldWhileWaiting = !m_probeSet;
  }

  TestDevice *m_device{nullptr};
  std::atomic<bool> m_probeSet{false};
  std::thread m_probe;
};

// What the callback observed. Written on the frame's thread, read after it
// has signalled `done`.
struct CallbackRecord
{
  TestDevice *device{nullptr};
  TestFrame *other{nullptr};
  bool mapped{false};
  bool committed{false};
  int ready{-1};
  bool completingHere{false};
  bool otherCompletingHere{true};
  bool completingOnAnotherThread{true};
};

void callbackIntoDevice(const void *userPtr, ANARIDevice, ANARIFrame f)
{
  auto &r = *(CallbackRecord *)userPtr;
  auto &d = *r.device;

  uint32_t w = 0, h = 0;
  ANARIDataType type = ANARI_UNKNOWN;
  r.mapped = d.frameBufferMap(f, "channel.color", &w, &h, &type) != nullptr;
  d.frameBufferUnmap(f, "channel.color");
  r.ready = d.frameReady(f, ANARI_NO_WAIT);
  float duration = 0.f;
  d.getProperty(
      f, "duration", ANARI_FLOAT32, &duration, sizeof(float), ANARI_WAIT);
  d.retain(f);
  d.release(f);
  d.discardFrame(f);
  int value = 7;
  d.setParameter(f, "value", ANARI_INT32, &value);
  d.commitParameters(f);
  r.committed = true;

  auto *frame = (TestFrame *)f;
  r.completingHere = frame->completingOnThisThread();
  r.otherCompletingHere = r.other->completingOnThisThread();
  std::thread([&] {
    r.completingOnAnotherThread = frame->completingOnThisThread();
  }).join();
}

// What a callback observed calling into the device on another frame. Written
// on the callback's thread, read after its frame has signalled `done`.
struct OtherFrameRecord
{
  TestDevice *device{nullptr};
  TestFrame *other{nullptr};
  int otherReady{-1};
  bool done{false};
};

void callbackIntoOtherFrame(const void *userPtr, ANARIDevice, ANARIFrame)
{
  auto &r = *(OtherFrameRecord *)userPtr;
  auto &d = *r.device;
  auto other = (ANARIFrame)r.other;

  r.otherReady = d.frameReady(other, ANARI_NO_WAIT);
  int value = 7;
  d.setParameter(other, "value", ANARI_INT32, &value);
  d.unsetParameter(other, "unused");
  d.commitParameters(other);
  float duration = 0.f;
  d.getProperty(other,
      "duration",
      ANARI_FLOAT32,
      &duration,
      sizeof(float),
      ANARI_NO_WAIT);
  d.retain(other);
  d.release(other);
  r.done = true;
}

// What a helide completion callback observed calling into another frame.
// Written on helide's worker, read after the app has waited on its frame.
struct HelideOtherFrameRecord
{
  anari::Frame other{nullptr};
  // Set by the app thread just before it waits on (or maps) 'other'.
  Event appWaiting;
  // Whether the callback waits for 'appWaiting' (and for the app thread to
  // block on 'other') first.
  bool waitForApp{true};
  int readyNoWait{-1};
  int readyWait{-1};
  bool mapped{false};
  bool queried{false};
  bool done{false};
};

void callIntoOtherFrameFromCallback(
    const void *userPtr, ANARIDevice d, ANARIFrame)
{
  auto &r = *(HelideOtherFrameRecord *)userPtr;
  if (r.waitForApp) {
    r.appWaiting.wait();
    // Give the app thread time to block in its wait on 'other'.
    std::this_thread::sleep_for(100ms);
  }

  anari::setParameter(d, r.other, "size", anari::math::uint2(4, 4));
  anari::commitParameters(d, r.other);
  r.readyNoWait = anariFrameReady(d, r.other, ANARI_NO_WAIT);
  r.readyWait = anariFrameReady(d, r.other, ANARI_WAIT);
  uint32_t width = 0, height = 0;
  ANARIDataType type = ANARI_UNKNOWN;
  r.mapped = anariMapFrame(d, r.other, "channel.color", &width, &height, &type)
      != nullptr;
  if (r.mapped)
    anariUnmapFrame(d, r.other, "channel.color");
  anari::render(d, r.other);
  float duration = -1.f;
  r.queried = anari::getProperty(d, r.other, "duration", duration, ANARI_WAIT);
  anari::retain(d, r.other);
  anari::release(d, r.other);
  r.done = true;
}

// What a helide completion callback observed.
struct HelideRecord
{
  anari::math::float4 pixel{-1.f};
  int ready{-1};
};

void mapFromCallback(const void *userPtr, ANARIDevice d, ANARIFrame f)
{
  auto &r = *(HelideRecord *)userPtr;
  auto mapped = anari::map<anari::math::float4>(d, f, "channel.color");
  if (mapped.data)
    r.pixel = mapped.data[0];
  anari::unmap(d, f, "channel.color");
  r.ready = anari::isReady(d, f);
}

// What a helide completion callback observed mapping an array, and the frame
// duration it read.
struct ArrayCallbackRecord
{
  anari::Array1D array{nullptr};
  bool mapped{false};
  float duration{-1.f};
};

void mapArrayFromCallback(const void *userPtr, ANARIDevice d, ANARIFrame f)
{
  auto &r = *(ArrayCallbackRecord *)userPtr;
  auto *values = anari::map<float>(d, r.array);
  r.mapped = values != nullptr;
  if (values)
    values[0] = 1.f;
  anari::unmap(d, r.array);
  anari::getProperty(d, f, "duration", r.duration, ANARI_NO_WAIT);
}

// What a helide completion callback observed querying its frame's world.
struct WorldCallbackRecord
{
  anari::World world{nullptr};
  int found{-1};
};

void queryWorldFromCallback(const void *userPtr, ANARIDevice d, ANARIFrame)
{
  auto &r = *(WorldCallbackRecord *)userPtr;
  float bounds[6] = {};
  r.found = anariGetProperty(d,
      r.world,
      "bounds",
      ANARI_FLOAT32_BOX3,
      bounds,
      sizeof(bounds),
      ANARI_WAIT);
}

// A 4x4 helide frame of an empty world with the given renderer 'background',
// which calls 'callback' with 'userPtr' when it completes.
anari::Frame newCallbackFrame(anari::Device d,
    ANARIFrameCompletionCallback callback,
    void *userPtr,
    const anari::math::float4 &background = anari::math::float4(0.f))
{
  auto world = anari::newObject<anari::World>(d);
  anari::commitParameters(d, world);
  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  anari::commitParameters(d, camera);
  auto renderer = anari::newObject<anari::Renderer>(d, "default");
  anari::setParameter(d, renderer, "background", background);
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", anari::math::uint2(4, 4));
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setAndReleaseParameter(d, frame, "world", world);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "frameCompletionCallback", callback);
  anari::setParameter(d, frame, "frameCompletionCallbackUserData", userPtr);
  anari::commitParameters(d, frame);
  return frame;
}

// Renders 'frame' and waits for it on another thread, so that a deadlocked
// callback fails the test rather than hanging it; true if the wait returned.
bool renderAndWait(anari::Device d, anari::Frame frame)
{
  anari::render(d, frame);
  std::promise<void> waited;
  auto waitResult = waited.get_future();
  std::thread([d, frame, p = std::move(waited)]() mutable {
    anari::wait(d, frame);
    p.set_value();
  }).detach();
  return waitResult.wait_for(10s) == std::future_status::ready;
}

} // namespace

SCENARIO("a completion callback can call into the device on its own frame",
    "[helium_frame_callback]")
{
  // Leaked on purpose: if the callback deadlocks, its threads still use them.
  auto *device = new TestDevice;
  auto *frame = new TestFrame(device->state());
  auto *other = new TestFrame(device->state());
  auto *record = new CallbackRecord;
  record->device = device;
  record->other = other;
  frame->callback = callbackIntoDevice;
  frame->callbackUserPtr = record;
  frame->device = (ANARIDevice)device;
  auto f = (ANARIFrame)frame;

  GIVEN("an app thread blocked in frameReady(ANARI_WAIT) on the frame")
  {
    device->renderFrame(f);
    std::promise<int> ready;
    auto readyResult = ready.get_future();
    std::thread([device, f, p = std::move(ready)]() mutable {
      p.set_value(device->frameReady(f, ANARI_WAIT));
    }).detach();

    THEN("the callback's calls on that frame do not deadlock")
    {
      REQUIRE(readyResult.wait_for(5s) == std::future_status::ready);
      REQUIRE(readyResult.get() == 1);
      frame->done.wait();

      CHECK(record->mapped);
      CHECK(record->ready == 1);
      CHECK(record->committed);
      CHECK(frame->getParam<int>("value", 0) == 7);

      // Only that frame, on the callback's thread, counts as completing.
      CHECK(record->completingHere);
      CHECK_FALSE(record->otherCompletingHere);
      CHECK_FALSE(record->completingOnAnotherThread);
      CHECK_FALSE(frame->completingOnThisThread());

      device->state()->commitBuffer.clear();
      frame->refDec(helium::RefType::PUBLIC);
      other->refDec(helium::RefType::PUBLIC);
      delete record;
      delete device;
    }
  }
}

SCENARIO("a helide completion callback can map its frame while the app waits",
    "[helide][helium_frame_callback]")
{
  anari::Library lib = anari::loadLibrary("helide");
  if (lib == nullptr) {
    WARN("helide library not available; skipping helide callback test");
    return;
  }

  // Leaked on purpose: if the callback deadlocks, its thread still uses them.
  anari::Device d = anari::newDevice(lib, "default");
  auto *record = new HelideRecord;
  const anari::math::float4 background(0.25f, 0.5f, 0.75f, 1.f);
  auto frame = newCallbackFrame(d, mapFromCallback, record, background);

  REQUIRE(renderAndWait(d, frame));
  CHECK(record->ready == 1);
  CHECK(record->pixel.x == background.x);
  CHECK(record->pixel.y == background.y);
  CHECK(record->pixel.z == background.z);

  anari::release(d, frame);
  anari::release(d, d);
  anari::unloadLibrary(lib);
  delete record;
}

SCENARIO(
    "a helide completion callback can map an array and sees its frame's "
    "duration",
    "[helide][helium_frame_callback]")
{
  anari::Library lib = anari::loadLibrary("helide");
  if (lib == nullptr) {
    WARN("helide library not available; skipping helide callback test");
    return;
  }

  // Leaked on purpose: if the callback deadlocks, its thread still uses them.
  anari::Device d = anari::newDevice(lib, "default");
  auto *record = new ArrayCallbackRecord;
  record->array = anari::newArray1D(d, ANARI_FLOAT32, 4);
  auto frame = newCallbackFrame(d, mapArrayFromCallback, record);

  REQUIRE(renderAndWait(d, frame));
  CHECK(record->mapped);
  // The duration of this frame, not the previous frame's (0).
  CHECK(record->duration > 0.f);

  anari::release(d, frame);
  anari::release(d, record->array);
  anari::release(d, d);
  anari::unloadLibrary(lib);
  delete record;
}

SCENARIO("a helide completion callback can WAIT on a query of its world",
    "[helide][helium_frame_callback]")
{
  anari::Library lib = anari::loadLibrary("helide");
  if (lib == nullptr) {
    WARN("helide library not available; skipping helide callback test");
    return;
  }

  // Leaked on purpose: if the callback deadlocks, its thread still uses them.
  anari::Device d = anari::newDevice(lib, "default");
  auto *record = new WorldCallbackRecord;
  auto frame = newCallbackFrame(d, queryWorldFromCallback, record);
  record->world = anari::newObject<anari::World>(d);
  anari::commitParameters(d, record->world);
  anari::setParameter(d, frame, "world", record->world);
  anari::commitParameters(d, frame);

  REQUIRE(renderAndWait(d, frame));
  CHECK(record->found != -1);

  anari::release(d, frame);
  anari::release(d, record->world);
  anari::release(d, d);
  anari::unloadLibrary(lib);
  delete record;
}

SCENARIO(
    "a completion callback can call into the device on another frame an app "
    "thread waits on",
    "[helium_frame_callback]")
{
  // Leaked on purpose: if the callback deadlocks, its threads still use them.
  auto *device = new TestDevice;
  auto *frame = new TestFrame(device->state());
  auto *other = new TestFrame(device->state());
  auto *record = new OtherFrameRecord;
  record->device = device;
  record->other = other;
  frame->callback = callbackIntoOtherFrame;
  frame->callbackUserPtr = record;
  frame->device = (ANARIDevice)device;
  // As on a single-worker device: 'frame' completes once the app waits on
  // 'other', whose render is queued behind it.
  frame->renderGate = &other->appWaiting;
  other->renderGate = &frame->done;
  auto f = (ANARIFrame)frame;
  auto o = (ANARIFrame)other;
  device->renderFrame(f);
  device->renderFrame(o);

  std::promise<bool> waited;
  auto waitResult = waited.get_future();

  GIVEN("an app thread blocked in frameReady(ANARI_WAIT) on the other frame")
  {
    std::thread([device, o, p = std::move(waited)]() mutable {
      p.set_value(device->frameReady(o, ANARI_WAIT) == 1);
    }).detach();
  }

  GIVEN("an app thread blocked mapping the other frame")
  {
    std::thread([device, o, p = std::move(waited)]() mutable {
      uint32_t w = 0, h = 0;
      ANARIDataType type = ANARI_UNKNOWN;
      p.set_value(
          device->frameBufferMap(o, "channel.color", &w, &h, &type) != nullptr);
    }).detach();
  }

  // Each GIVEN ends here with the app thread started.
  REQUIRE(waitResult.wait_for(5s) == std::future_status::ready);
  CHECK(waitResult.get());
  frame->done.wait();
  other->done.wait();

  CHECK(record->done);
  CHECK(record->otherReady == 0);
  CHECK(other->getParam<int>("value", 0) == 7);

  device->state()->commitBuffer.clear();
  frame->refDec(helium::RefType::PUBLIC);
  other->refDec(helium::RefType::PUBLIC);
  delete record;
  delete device;
}

SCENARIO(
    "a helide completion callback calling into another frame an app thread "
    "waits on reports the waits it can't make instead of hanging",
    "[helide][helium_frame_callback]")
{
  helide_test::StatusLog log;
  anari::Library lib =
      anari::loadLibrary("helide", helide_test::collectStatus, &log);
  if (lib == nullptr) {
    WARN("helide library not available; skipping helide callback test");
    return;
  }

  // Leaked on purpose: if the callback deadlocks, its thread still uses them.
  anari::Device d = anari::newDevice(lib, "default");
  auto *record = new HelideOtherFrameRecord;
  auto frame = newCallbackFrame(d, callIntoOtherFrameFromCallback, record);
  record->other = newCallbackFrame(d, nullptr, nullptr);

  // helide's single worker renders 'other' after 'frame's callback returns.
  anari::render(d, frame);
  anari::render(d, record->other);

  std::promise<bool> waited;
  auto waitResult = waited.get_future();

  GIVEN("an app thread blocked in anariFrameReady(ANARI_WAIT) on the other")
  {
    std::thread([d, record, p = std::move(waited)]() mutable {
      record->appWaiting.set();
      p.set_value(anariFrameReady(d, record->other, ANARI_WAIT) == 1);
    }).detach();
  }

  GIVEN("an app thread blocked mapping the other frame")
  {
    std::thread([d, record, p = std::move(waited)]() mutable {
      record->appWaiting.set();
      auto mapped =
          anari::map<anari::math::float4>(d, record->other, "channel.color");
      p.set_value(mapped.data != nullptr);
      anari::unmap(d, record->other, "channel.color");
    }).detach();
  }

  GIVEN(
      "an app thread rendering the other frame again, which waits for its "
      "previous render")
  {
    std::thread([d, record, p = std::move(waited)]() mutable {
      record->appWaiting.set();
      anari::render(d, record->other);
      p.set_value(true);
    }).detach();
  }

  // Each GIVEN ends here with the app thread started.
  REQUIRE(waitResult.wait_for(10s) == std::future_status::ready);
  CHECK(waitResult.get());
  anari::wait(d, frame);

  CHECK(record->done);
  // The other frame's render is queued behind the callback: its calls that
  // would wait for it are refused, the rest go through.
  CHECK(record->readyNoWait == 0);
  CHECK(record->readyWait == 0);
  CHECK_FALSE(record->mapped);
  CHECK(record->queried);
  CHECK(
      log.errors.contains("anariFrameReady() with ANARI_WAIT would deadlock"));
  CHECK(log.errors.contains("anariMapFrame() would deadlock"));
  CHECK(log.errors.contains("anariRenderFrame() would deadlock"));

  anari::wait(d, record->other);
  anari::release(d, frame);
  anari::release(d, record->other);
  anari::release(d, d);
  anari::unloadLibrary(lib);
  delete record;
}

SCENARIO("a helide completion callback can wait on and map a rendered frame",
    "[helide][helium_frame_callback]")
{
  helide_test::StatusLog log;
  anari::Library lib =
      anari::loadLibrary("helide", helide_test::collectStatus, &log);
  if (lib == nullptr) {
    WARN("helide library not available; skipping helide callback test");
    return;
  }

  // Leaked on purpose: if the callback deadlocks, its thread still uses them.
  anari::Device d = anari::newDevice(lib, "default");
  auto *record = new HelideOtherFrameRecord;
  record->waitForApp = false;
  auto frame = newCallbackFrame(d, callIntoOtherFrameFromCallback, record);
  record->other = newCallbackFrame(d, nullptr, nullptr);

  // The other frame's render ends before 'frame's is queued.
  anari::render(d, record->other);
  anari::wait(d, record->other);

  REQUIRE(renderAndWait(d, frame));
  CHECK(record->done);
  CHECK(record->readyNoWait == 1);
  CHECK(record->readyWait == 1);
  CHECK(record->mapped);
  CHECK(record->queried);
  CHECK(log.errors.empty());

  anari::wait(d, record->other);
  anari::release(d, frame);
  anari::release(d, record->other);
  anari::release(d, d);
  anari::unloadLibrary(lib);
  delete record;
}

SCENARIO(
    "a frame that doesn't opt in to waiting without its object lock is "
    "waited for under it",
    "[helium_frame_callback]")
{
  auto *device = new TestDevice;
  auto *frame = new LockedWaitFrame(device->state(), device);
  auto f = (ANARIFrame)frame;

  GIVEN("frameReady(ANARI_WAIT)")
  {
    CHECK(device->frameReady(f, ANARI_WAIT) == 1);
  }

  GIVEN("map")
  {
    uint32_t w = 0, h = 0;
    ANARIDataType type = ANARI_UNKNOWN;
    CHECK(device->frameBufferMap(f, "channel.color", &w, &h, &type) != nullptr);
  }

  frame->joinProbe();
  CHECK(frame->lockHeldWhileWaiting);
  CHECK(frame->getParam<int>("value", 0) == 1);

  frame->refDec(helium::RefType::PUBLIC);
  delete device;
}
