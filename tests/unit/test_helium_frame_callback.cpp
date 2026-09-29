// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Regressions for frame completion callbacks that call back into the device on
// the frame that is completing. BaseDevice holds a frame's object lock across
// blocking calls such as frameReady(ANARI_WAIT); if an app thread is blocked
// there when the callback runs, a callback call on that frame must not wait
// for the same lock.

#include "catch.hpp"
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

// renderFrame() completes on its own thread once an app thread is blocked in
// frameReady(ANARI_WAIT), then invokes the completion callback there.
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
      appWaiting.wait();
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
    appWaiting.set();
    done.wait();
    return 1;
  }

  void discard() override {}

  ANARIFrameCompletionCallback callback{nullptr};
  const void *callbackUserPtr{nullptr};
  ANARIDevice device{nullptr};
  Event appWaiting;
  Event done;
  std::atomic<bool> rendered{false};
  float pixel{0.5f};

 private:
  std::thread m_worker;
};

using helium_test::TestDevice;

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
