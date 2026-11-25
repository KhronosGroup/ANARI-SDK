// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Device death with a thread in anariFrameReady(ANARI_WAIT): the host crashes
// while a render is outstanding. The waiter is released, with the frame
// ready, and it is the waiter that tells the application of the death, on
// its own thread, before its call returns.

#include "DeathTest.h"

#include <atomic>

using namespace guard_test;

// The host crashes on the render itself, so nothing but the death can
// release the wait that follows.
static void renderNeverStarts(ANARILibrary library, Seen &seen)
{
  setFault("renderFrame:1:before:segv");
  ANARIDevice device = anariNewDevice(library, "default");
  CHECK(device);
  const pid_t host = testHost();
  CHECK(host > 0);

  Scene scene(device);
  anariRenderFrame(device, scene.frame);
  CHECK(anariFrameReady(device, scene.frame, ANARI_WAIT) == 1);

  checkDeathMessage(seen, device, "killed by signal 11");
  CHECK(!processExists(host));
  checkDeadDevice(scene);

  scene.release();
  anariRelease(device, device);
  CHECK(seen.fatal == 1);
  CHECK(seen.afterDeath == 0);
}

// Over a wrapped device whose render takes 200 ms, so that the waiter is
// known to be waiting when the host is killed. It is killed from outside, so
// that the waiter's is the only call the application is in or makes.
static void waiterIsWaiting(ANARILibrary library, Seen &seen)
{
  seen.reset();
  const char *wrapped = "slow@guard_recorder";
  clearFault();
  ANARIDevice device = anariNewDevice(library, wrapped);
  CHECK(device);
  const pid_t host = testHost();
  CHECK(host > 0);

  Scene scene(device);
  anariRenderFrame(device, scene.frame);

  std::atomic<bool> returned{false};
  int ready = -1;
  int fatalWhenReleased = -1;
  std::thread waiter([&] {
    ready = anariFrameReady(device, scene.frame, ANARI_WAIT);
    fatalWhenReleased = seen.fatalCount();
    returned = true;
  });
  const auto waiterThread = waiter.get_id();

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(!returned);
  CHECK(seen.fatalCount() == 0);
  CHECK(kill(host, SIGKILL) == 0);
  waiter.join();

  CHECK(ready == 1);
  // The death released it, not the end of the render, and it had told the
  // application by the time its call returned.
  CHECK(fatalWhenReleased == 1);
  checkDeathMessage(seen, device, "killed by signal 9", wrapped, waiterThread);
  CHECK(!processExists(host));
  checkDeadDevice(scene);

  scene.release();
  anariRelease(device, device);
  CHECK(seen.fatal == 1);
}

int main()
{
  Seen seen;
  ANARILibrary library = anariLoadLibrary("guard", Seen::callback, &seen);
  CHECK(library);

  renderNeverStarts(library, seen);
  waiterIsWaiting(library, seen);

  anariUnloadLibrary(library);
  std::printf("passed\n");
  return 0;
}
