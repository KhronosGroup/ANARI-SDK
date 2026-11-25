// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Device death with a frame in flight that has a completion callback: the
// callback runs exactly once for that render, whether the wrapped device had
// got as far as calling back or not, and a render asked of the dead device
// runs none. If the application is inside a call when the device dies, that
// call tells it of the death first and the callback follows; if it is not,
// the callback runs at once and the death is told by the next call, which
// may be one the callback makes.

#include "DeathTest.h"

#include <atomic>

using namespace guard_test;

struct Callbacks
{
  Seen *seen{nullptr};
  std::atomic<int> count{0};
  std::atomic<int> beforeDeathMessage{0};
  std::thread::id thread; // the one the latest callback ran on
  bool mapInside{false}; // make an ANARI call from inside the callback
  std::atomic<int> mappedNothing{0};
};

static void onFrameCompletion(
    const void *userData, ANARIDevice device, ANARIFrame frame)
{
  auto &callbacks = *static_cast<Callbacks *>(const_cast<void *>(userData));
  if (callbacks.seen->fatalCount() == 0)
    callbacks.beforeDeathMessage++;
  callbacks.thread = std::this_thread::get_id();
  if (callbacks.mapInside) {
    uint32_t width = 77, height = 77;
    ANARIDataType pixelType = ANARI_FLOAT32;
    if (!anariMapFrame(
            device, frame, "channel.color", &width, &height, &pixelType)
        && width == 0 && height == 0 && pixelType == ANARI_UNKNOWN)
      callbacks.mappedNothing++;
  }
  callbacks.count++;
}

static void setCallback(Scene &scene, Callbacks &callbacks)
{
  ANARIFrameCompletionCallback callback = onFrameCompletion;
  anariSetParameter(scene.device,
      scene.frame,
      "frameCompletionCallback",
      ANARI_FRAME_COMPLETION_CALLBACK,
      &callback);
  anariSetParameter(scene.device,
      scene.frame,
      "frameCompletionCallbackUserData",
      ANARI_VOID_POINTER,
      &callbacks);
  anariCommitParameters(scene.device, scene.frame);
}

// 'count' callbacks, and no more a moment later.
static void checkCount(Callbacks &callbacks, int count)
{
  CHECK(waitFor([&] { return callbacks.count >= count; }));
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  CHECK(callbacks.count == count);
}

static void finish(Scene &scene, Seen &seen, Callbacks &callbacks, int count)
{
  // A render asked of a dead device is not in flight: no callback.
  anariRenderFrame(scene.device, scene.frame);
  CHECK(anariFrameReady(scene.device, scene.frame, ANARI_WAIT) == 1);
  checkDeadDevice(scene);
  checkCount(callbacks, count);

  scene.release();
  anariRelease(scene.device, scene.device);
  CHECK(seen.fatal == 1);
  CHECK(seen.afterDeath == 0);
  CHECK(callbacks.count == count);
}

// The host crashes before the wrapped device hears of the render: the
// callback comes from the adapter. The application is in anariFrameReady by
// then, or all but, so which of the callback and the death message comes
// first is not checked here.
static void renderNeverStarts(ANARILibrary library, Seen &seen)
{
  seen.reset();
  Callbacks callbacks;
  callbacks.seen = &seen;
  setFault("renderFrame:1:before:segv");
  ANARIDevice device = anariNewDevice(library, "default");
  CHECK(device);
  const pid_t host = testHost();
  CHECK(host > 0);

  Scene scene(device);
  setCallback(scene, callbacks);
  anariRenderFrame(device, scene.frame);
  CHECK(anariFrameReady(device, scene.frame, ANARI_WAIT) == 1);

  checkDeathMessage(seen, device, "killed by signal 11");
  CHECK(!processExists(host));
  checkCount(callbacks, 1);
  finish(scene, seen, callbacks, 1);
}

// The three that follow are over a wrapped device whose render takes 200 ms,
// with the host killed from outside 50 ms into it, so that what the
// application is doing when the device dies is known.

// The application is waiting in anariFrameReady: that call tells it of the
// death, on its own thread, and only then does the callback run, on another.
static void callerInsideACall(ANARILibrary library, Seen &seen)
{
  seen.reset();
  Callbacks callbacks;
  callbacks.seen = &seen;
  const char *wrapped = "slow@guard_recorder";
  clearFault();
  ANARIDevice device = anariNewDevice(library, wrapped);
  CHECK(device);
  const pid_t host = testHost();
  CHECK(host > 0);

  Scene scene(device);
  setCallback(scene, callbacks);
  anariRenderFrame(device, scene.frame);
  std::thread killer([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(callbacks.count == 0);
    CHECK(kill(host, SIGKILL) == 0);
  });
  CHECK(anariFrameReady(device, scene.frame, ANARI_WAIT) == 1);
  checkDeathMessage(seen, device, "killed by signal 9", wrapped);
  killer.join();

  checkCount(callbacks, 1);
  CHECK(callbacks.beforeDeathMessage == 0);
  CHECK(callbacks.thread != std::this_thread::get_id());
  finish(scene, seen, callbacks, 1);
}

// The application is in no call: the callback runs at once, and the death
// is told by the call the application makes next.
static void callerIdle(ANARILibrary library, Seen &seen)
{
  seen.reset();
  Callbacks callbacks;
  callbacks.seen = &seen;
  const char *wrapped = "slow@guard_recorder";
  clearFault();
  ANARIDevice device = anariNewDevice(library, wrapped);
  CHECK(device);
  const pid_t host = testHost();
  CHECK(host > 0);

  Scene scene(device);
  setCallback(scene, callbacks);
  anariRenderFrame(device, scene.frame);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(callbacks.count == 0);
  CHECK(kill(host, SIGKILL) == 0);

  checkCount(callbacks, 1);
  CHECK(callbacks.beforeDeathMessage == 1);
  CHECK(seen.fatalCount() == 0);

  anariCommitParameters(device, scene.camera);
  checkDeathMessage(seen, device, "killed by signal 9", wrapped);
  finish(scene, seen, callbacks, 1);
}

// The same, with a callback that makes an ANARI call: that call is the next
// one, so it tells of the death, on the thread the callback runs on.
static void callbackMakesTheNextCall(ANARILibrary library, Seen &seen)
{
  seen.reset();
  Callbacks callbacks;
  callbacks.seen = &seen;
  callbacks.mapInside = true;
  const char *wrapped = "slow@guard_recorder";
  clearFault();
  ANARIDevice device = anariNewDevice(library, wrapped);
  CHECK(device);
  const pid_t host = testHost();
  CHECK(host > 0);

  Scene scene(device);
  setCallback(scene, callbacks);
  anariRenderFrame(device, scene.frame);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(callbacks.count == 0);
  CHECK(kill(host, SIGKILL) == 0);

  checkCount(callbacks, 1);
  CHECK(callbacks.beforeDeathMessage == 1);
  CHECK(callbacks.mappedNothing == 1);
  CHECK(callbacks.thread != std::this_thread::get_id());
  checkDeathMessage(
      seen, device, "killed by signal 9", wrapped, callbacks.thread);

  // The application's own next call has nothing left to tell.
  anariCommitParameters(device, scene.camera);
  callbacks.mapInside = false;
  finish(scene, seen, callbacks, 1);
}

// The host crashes just after the wrapped device was given the render. By
// then the wrapped device may or may not have called back and the host may or
// may not have said so; whichever it was, one callback. A small render can
// even be over, and the host have said that too: then the wait ends with no
// death to tell of, and the application's next call tells.
static void renderJustStarted(ANARILibrary library, Seen &seen)
{
  int fromHost = 0;
  int overFirst = 0;
  const int rounds = 10;
  for (int round = 0; round < rounds; round++) {
    seen.reset();
    Callbacks callbacks;
    callbacks.seen = &seen;
    setFault("renderFrame:2:after:segv");
    ANARIDevice device = anariNewDevice(library, "default");
    CHECK(device);
    const pid_t host = testHost();
    CHECK(host > 0);

    Scene scene(device);
    setCallback(scene, callbacks);
    anariRenderFrame(device, scene.frame);
    CHECK(anariFrameReady(device, scene.frame, ANARI_WAIT) == 1);
    checkCount(callbacks, 1);
    CHECK(seen.fatalCount() == 0);

    scene.change();
    anariRenderFrame(device, scene.frame);
    CHECK(anariFrameReady(device, scene.frame, ANARI_WAIT) == 1);
    if (checkNotTold(seen, host)) {
      overFirst++;
      anariCommitParameters(device, scene.camera);
    }

    checkDeathMessage(seen, device, "killed by signal 11");
    CHECK(!processExists(host));
    checkCount(callbacks, 2);
    // The first render's, and this one's if the host passed it on in time.
    fromHost += callbacks.beforeDeathMessage - 1;
    finish(scene, seen, callbacks, 2);
  }
  std::printf(
      "render just started: in %d of %d rounds the wrapped device's "
      "callback got through before the death, and in %d the render "
      "was over\n",
      fromHost,
      rounds,
      overFirst);
}

// Over a wrapped device that calls back at once and finishes the render
// 200 ms later: the host has reported the callback when it crashes, so the
// adapter must not run it a second time.
static void wrappedDeviceHadCalledBack(ANARILibrary library, Seen &seen)
{
  seen.reset();
  Callbacks callbacks;
  callbacks.seen = &seen;
  const char *wrapped = "late@guard_recorder";
  setFault("renderFrame:1:after:segv");
  ANARIDevice device = anariNewDevice(library, wrapped);
  CHECK(device);
  const pid_t host = testHost();
  CHECK(host > 0);

  Scene scene(device);
  setCallback(scene, callbacks);
  anariRenderFrame(device, scene.frame);
  CHECK(anariFrameReady(device, scene.frame, ANARI_WAIT) == 1);

  checkDeathMessage(seen, device, "killed by signal 11", wrapped);
  CHECK(!processExists(host));
  checkCount(callbacks, 1);
  // This one came from the wrapped device, while it was alive.
  CHECK(callbacks.beforeDeathMessage == 1);
  finish(scene, seen, callbacks, 1);
}

int main()
{
  Seen seen;
  ANARILibrary library = anariLoadLibrary("guard", Seen::callback, &seen);
  CHECK(library);

  renderNeverStarts(library, seen);
  renderJustStarted(library, seen);
  wrappedDeviceHadCalledBack(library, seen);
  callerInsideACall(library, seen);
  callerIdle(library, seen);
  callbackMakesTheNextCall(library, seen);

  anariUnloadLibrary(library);
  std::printf("passed\n");
  return 0;
}
