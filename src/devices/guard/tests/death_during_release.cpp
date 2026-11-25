// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Device death during the release of the device: the host crashes on the
// release itself, before or after the wrapped device is released. The
// application's anariRelease returns, and by then it has been told.

#include "DeathTest.h"

using namespace guard_test;

static void run(
    ANARILibrary library, Seen &seen, const char *fault, const char *how)
{
  seen.reset();
  setFault(fault);
  ANARIDevice device = anariNewDevice(library, "default");
  CHECK(device);
  const pid_t host = testHost();
  CHECK(host > 0);

  Scene scene(device);
  anariRenderFrame(device, scene.frame);
  CHECK(anariFrameReady(device, scene.frame, ANARI_WAIT) == 1);
  scene.release();
  CHECK(seen.fatalCount() == 0);

  anariRelease(device, device);

  // Nothing to wait for: the message came before the release returned.
  CHECK(seen.fatalCount() == 1);
  checkDeathMessage(seen, device, how);
  CHECK(!processExists(host));
  CHECK(testHost() == -1);
  CHECK(seen.afterDeath == 0);
}

int main()
{
  Seen seen;
  ANARILibrary library = anariLoadLibrary("guard", Seen::callback, &seen);
  CHECK(library);

  run(library, seen, "releaseDevice:1:before:segv", "killed by signal 11");
  // The wrapped device has been released, and the reply is never sent.
  run(library, seen, "releaseDevice:1:after:exit", "exited with code 1");

  // A device released while frames and arrays are still held, and the host
  // crashing on an object's release just before.
  seen.reset();
  setFault("release:1:before:segv");
  ANARIDevice device = anariNewDevice(library, "default");
  CHECK(device);
  Scene scene(device);
  anariRelease(device, scene.camera);
  anariRelease(device, device);
  CHECK(seen.fatalCount() == 1);
  checkDeathMessage(seen, device, "killed by signal 11");
  CHECK(testHost() == -1);

  anariUnloadLibrary(library);
  std::printf("passed\n");
  return 0;
}
