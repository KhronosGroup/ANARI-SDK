// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Regressions for frame completion callbacks that call back into the device on
// the frame that is completing. BaseDevice holds a frame's object lock across
// blocking calls such as frameReady(ANARI_WAIT); if an app thread is blocked
// there when the callback runs, a callback call on that frame must not wait
// for the same lock.

#include "catch.hpp"

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

struct TestDevice : public helium::BaseDevice
{
  TestDevice() : BaseDevice(nullptr, nullptr)
  {
    m_state = std::make_unique<helium::BaseGlobalDeviceState>(this_device());
  }

  helium::BaseGlobalDeviceState *state()
  {
    return m_state.get();
  }

  ANARIArray1D newArray1D(const void *,
      ANARIMemoryDeleter,
      const void *,
      ANARIDataType,
      uint64_t) override
  {
    return nullptr;
  }
  ANARIArray2D newArray2D(const void *,
      ANARIMemoryDeleter,
      const void *,
      ANARIDataType,
      uint64_t,
      uint64_t) override
  {
    return nullptr;
  }
  ANARIArray3D newArray3D(const void *,
      ANARIMemoryDeleter,
      const void *,
      ANARIDataType,
      uint64_t,
      uint64_t,
      uint64_t) override
  {
    return nullptr;
  }
  ANARIGeometry newGeometry(const char *) override
  {
    return nullptr;
  }
  ANARIMaterial newMaterial(const char *) override
  {
    return nullptr;
  }
  ANARISampler newSampler(const char *) override
  {
    return nullptr;
  }
  ANARISurface newSurface() override
  {
    return nullptr;
  }
  ANARISpatialField newSpatialField(const char *) override
  {
    return nullptr;
  }
  ANARIVolume newVolume(const char *) override
  {
    return nullptr;
  }
  ANARILight newLight(const char *) override
  {
    return nullptr;
  }
  ANARIGroup newGroup() override
  {
    return nullptr;
  }
  ANARIInstance newInstance(const char *) override
  {
    return nullptr;
  }
  ANARIWorld newWorld() override
  {
    return nullptr;
  }
  ANARICamera newCamera(const char *) override
  {
    return nullptr;
  }
  ANARIRenderer newRenderer(const char *) override
  {
    return nullptr;
  }
  ANARIFrame newFrame() override
  {
    return nullptr;
  }
  const char **getObjectSubtypes(ANARIDataType) override
  {
    return nullptr;
  }
  const void *getObjectInfo(
      ANARIDataType, const char *, const char *, ANARIDataType) override
  {
    return nullptr;
  }
  const void *getParameterInfo(ANARIDataType,
      const char *,
      const char *,
      ANARIDataType,
      const char *,
      ANARIDataType) override
  {
    return nullptr;
  }
};

// What the callback observed. Written on the frame's thread, read after it
// has signalled `done`.
struct CallbackRecord
{
  TestDevice *device{nullptr};
  TestFrame *other{nullptr};
  bool mapped{false};
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

      // Only that frame, on the callback's thread, counts as completing.
      CHECK(record->completingHere);
      CHECK_FALSE(record->otherCompletingHere);
      CHECK_FALSE(record->completingOnAnotherThread);
      CHECK_FALSE(frame->completingOnThisThread());

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
  anari::setParameter(d, frame, "world", world);
  anari::setParameter(d, frame, "camera", camera);
  anari::setParameter(d, frame, "renderer", renderer);
  anari::setParameter(d,
      frame,
      "frameCompletionCallback",
      (ANARIFrameCompletionCallback)mapFromCallback);
  anari::setParameter(
      d, frame, "frameCompletionCallbackUserData", (void *)record);
  anari::commitParameters(d, frame);

  anari::render(d, frame);
  std::promise<void> waited;
  auto waitResult = waited.get_future();
  std::thread([d, frame, p = std::move(waited)]() mutable {
    anari::wait(d, frame);
    p.set_value();
  }).detach();

  REQUIRE(waitResult.wait_for(10s) == std::future_status::ready);
  CHECK(record->ready == 1);
  CHECK(record->pixel.x == background.x);
  CHECK(record->pixel.y == background.y);
  CHECK(record->pixel.z == background.z);

  anari::release(d, frame);
  anari::release(d, renderer);
  anari::release(d, camera);
  anari::release(d, world);
  anari::release(d, d);
  anari::unloadLibrary(lib);
  delete record;
}
