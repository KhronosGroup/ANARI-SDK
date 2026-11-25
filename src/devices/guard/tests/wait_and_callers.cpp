// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// A thread waiting in anariFrameReady(ANARI_WAIT) is not in the way of
// anything: while it waits another thread calls on objects of its own, and
// the frame's completion callback calls on the waited frame itself. That
// holds while the render goes on, and when the host dies under all three.
//
// Over a wrapped device that calls back as soon as a render is asked for and
// whose renders are over 200 ms later, so that the waiter is known to wait.

#include "DeathTest.h"

#include <atomic>

using namespace guard_test;

static const char *const WRAPPED = "late@guard_recorder";

struct Callback
{
  Seen *seen{nullptr};
  // With device death: keep calling on the frame until told to stop
  std::atomic<bool> untilStopped{false};
  std::atomic<bool> stop{false};
  std::atomic<int> runs{0};
  std::atomic<int> calls{0};
  std::atomic<int> answers{0}; // of those, how many the wrapped device gave
  std::thread::id thread;

  static void onCompletion(
      const void *userData, ANARIDevice d, ANARIFrame frame)
  {
    auto &self = *static_cast<Callback *>(const_cast<void *>(userData));
    self.thread = std::this_thread::get_id();
    do {
      int32_t answer = 0;
      if (anariGetProperty(
              d, frame, "answer", ANARI_INT32, &answer, 4, ANARI_WAIT)
          && answer == 42)
        self.answers++;
      anariFrameReady(d, frame, ANARI_NO_WAIT);
      self.calls++;
    } while (self.untilStopped && !self.stop && self.seen->fatalCount() == 0);
    self.runs++;
  }
};

static void setCallback(Scene &scene, Callback &callback)
{
  const ANARIFrameCompletionCallback function = Callback::onCompletion;
  anariSetParameter(scene.device,
      scene.frame,
      "frameCompletionCallback",
      ANARI_FRAME_COMPLETION_CALLBACK,
      &function);
  anariSetParameter(scene.device,
      scene.frame,
      "frameCompletionCallbackUserData",
      ANARI_VOID_POINTER,
      &callback);
  anariCommitParameters(scene.device, scene.frame);
}

// One round of calls on an object that is the calling thread's alone. True
// if the wrapped device answered.
static bool callOnOwnObject(ANARIDevice d)
{
  ANARIGeometry geometry = anariNewGeometry(d, "triangle");
  CHECK(geometry);
  const float radius = 2.f;
  anariSetParameter(d, geometry, "radius", ANARI_FLOAT32, &radius);
  anariCommitParameters(d, geometry);
  int32_t answer = 0;
  const bool answered =
      anariGetProperty(
          d, geometry, "answer", ANARI_INT32, &answer, 4, ANARI_WAIT)
      && answer == 42;
  anariRelease(d, geometry);
  return answered;
}

static void whileRendering(ANARILibrary library, Seen &seen)
{
  clearFault();
  ANARIDevice device = anariNewDevice(library, WRAPPED);
  CHECK(device);

  Scene scene(device);
  Callback callback;
  callback.seen = &seen;
  setCallback(scene, callback);

  for (int render = 1; render <= 2; render++) {
    const auto start = std::chrono::steady_clock::now();
    anariRenderFrame(device, scene.frame);

    std::atomic<bool> returned{false};
    int ready = -1;
    std::thread waiter([&] {
      ready = anariFrameReady(device, scene.frame, ANARI_WAIT);
      returned = true;
    });
    // Rounds the wrapped device answered, begun and ended inside the wait
    int answeredWhileWaiting = 0;
    std::thread caller([&] {
      while (!returned) {
        const bool answered = callOnOwnObject(device);
        CHECK(answered);
        if (!returned)
          answeredWhileWaiting++;
      }
    });
    waiter.join();
    const auto waited = std::chrono::steady_clock::now() - start;
    caller.join();

    CHECK(ready == 1);
    CHECK(waited >= std::chrono::milliseconds(150));
    // The caller's calls do not keep the host from seeing the render end.
    CHECK(waited < std::chrono::milliseconds(1500));
    CHECK(answeredWhileWaiting >= 10);
    CHECK(waitFor([&] { return callback.runs == render; }));
    CHECK(callback.calls == render);
    CHECK(callback.answers == render);
    CHECK(callback.thread != std::this_thread::get_id());
    CHECK(anariFrameReady(device, scene.frame, ANARI_NO_WAIT) == 1);
    scene.change();
  }

  scene.release();
  anariRelease(device, device);
  CHECK(seen.fatalCount() == 0);
}

static void atDeath(ANARILibrary library, Seen &seen)
{
  seen.reset();
  clearFault();
  ANARIDevice device = anariNewDevice(library, WRAPPED);
  CHECK(device);
  const pid_t host = testHost();
  CHECK(host > 0);

  Scene scene(device);
  Callback callback;
  callback.seen = &seen;
  callback.untilStopped = true;
  setCallback(scene, callback);

  anariRenderFrame(device, scene.frame);

  std::atomic<bool> returned{false};
  int ready = -1;
  std::thread waiter([&] {
    ready = anariFrameReady(device, scene.frame, ANARI_WAIT);
    returned = true;
  });
  std::atomic<int> answered{0};
  std::atomic<bool> callerDone{false};
  std::thread caller([&] {
    while (seen.fatalCount() == 0)
      answered += callOnOwnObject(device) ? 1 : 0;
    // On a device it knows is dead: nothing answers.
    for (int i = 0; i < 20; i++)
      CHECK(!callOnOwnObject(device));
    callerDone = true;
  });
  const auto waiterThread = waiter.get_id();
  const auto callerThread = caller.get_id();

  // All three are at it: the waiter waiting, the caller and the callback
  // being answered.
  CHECK(waitFor([&] { return answered >= 5 && callback.answers >= 5; }));
  CHECK(!returned);
  CHECK(seen.fatalCount() == 0);
  CHECK(kill(host, SIGKILL) == 0);
  // The callback is the application's: it has to return for the dispatcher
  // to get on, and so for any other thread's call to tell of the death.
  callback.stop = true;

  if (!waitFor([&] { return returned && callerDone; }, 30.0)) {
    std::fprintf(stderr,
        "FAILED: after device death the %s did not return\n",
        returned ? "caller" : "waiter");
    std::exit(1);
  }
  waiter.join();
  caller.join();

  CHECK(ready == 1);
  std::thread::id teller;
  {
    std::lock_guard<std::mutex> lock(seen.mutex);
    CHECK(seen.fatal == 1);
    teller = seen.thread;
  }
  CHECK(teller == waiterThread || teller == callerThread
      || teller == callback.thread);
  checkDeathMessage(seen, device, "killed by signal 9", WRAPPED, teller);
  CHECK(!processExists(host));
  checkDeadDevice(scene);

  // The wrapped device had called back before it died, so the callback ran
  // for that render and is not run again.
  CHECK(waitFor([&] { return callback.runs == 1; }));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(callback.runs == 1);

  scene.release();
  anariRelease(device, device);
  CHECK(seen.fatal == 1);
}

int main()
{
  Seen seen;
  ANARILibrary library = anariLoadLibrary("guard", Seen::callback, &seen);
  CHECK(library);

  whileRendering(library, seen);
  for (int repeat = 0; repeat < 3; repeat++)
    atDeath(library, seen);

  anariUnloadLibrary(library);
  std::printf("passed\n");
  return 0;
}
