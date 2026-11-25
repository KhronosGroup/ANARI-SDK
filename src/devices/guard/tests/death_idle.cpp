// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Device death with the application idle: the host crashes at a call nobody
// is waiting on. The application hears nothing until its next ANARI call on
// the device, whichever call that is, and is told by that call, on the thread
// that makes it, before it returns: once, by a message that says how the host
// ended. It carries on calling ANARI on the dead device.

#include "DeathTest.h"

using namespace guard_test;

static int toldByTheFaultingCall = 0;

// One device, dead while idle, whose next call is 'firstCall'.
template <typename F>
static void run(ANARILibrary library, Seen &seen, F &&firstCall)
{
  seen.reset();
  setFault("discardFrame:1:after:segv");
  ANARIDevice device = anariNewDevice(library, "default");
  CHECK(device);
  const pid_t host = testHost();
  CHECK(host > 0);

  // A working device: a frame rendered and read back.
  Scene scene(device);
  anariRenderFrame(device, scene.frame);
  CHECK(anariFrameReady(device, scene.frame, ANARI_WAIT) == 1);
  uint32_t width = 0, height = 0;
  ANARIDataType pixelType = ANARI_UNKNOWN;
  CHECK(anariMapFrame(
      device, scene.frame, "channel.color", &width, &height, &pixelType));
  CHECK(width == scene.size[0] && height == scene.size[1]);
  anariUnmapFrame(device, scene.frame, "channel.color");
  CHECK(seen.fatalCount() == 0);

  // Nothing is returned by this call, so nobody waits for the host that
  // crashes on it.
  anariDiscardFrame(device, scene.frame);
  if (checkNotTold(seen, host))
    firstCall(scene);
  else
    toldByTheFaultingCall++;
  checkDeathMessage(seen, device, "killed by signal 11");
  CHECK(!processExists(host));
  checkDeadDevice(scene);

  scene.release();
  anariRelease(device, device);
  CHECK(seen.fatal == 1);
  CHECK(seen.afterDeath == 0);
}

int main()
{
  Seen seen;
  ANARILibrary library = anariLoadLibrary("guard", Seen::callback, &seen);
  CHECK(library);

  // A call that would have been sent.
  run(library, seen, [](Scene &scene) {
    anariCommitParameters(scene.device, scene.camera);
  });
  // A call that would have waited for a reply.
  run(library, seen, [](Scene &scene) {
    float duration = -1.f;
    CHECK(anariGetProperty(scene.device,
              scene.frame,
              "duration",
              ANARI_FLOAT32,
              &duration,
              sizeof(duration),
              ANARI_WAIT)
        == 0);
  });
  // Calls the adapter answers by itself.
  run(library, seen, [](Scene &scene) {
    CHECK(anariFrameReady(scene.device, scene.frame, ANARI_NO_WAIT) == 1);
  });
  run(library, seen, [&](Scene &scene) {
    anariRetain(scene.device, scene.frame);
    CHECK(seen.fatalCount() == 1);
    anariRelease(scene.device, scene.frame);
  });
  run(library, seen, [&](Scene &scene) {
    anariRetain(scene.device, scene.device);
    CHECK(seen.fatalCount() == 1);
    anariRelease(scene.device, scene.device);
  });
  run(library, seen, [](Scene &scene) {
    anariUnmapFrame(scene.device, scene.frame, "channel.color");
  });

  anariUnloadLibrary(library);
  std::printf(
      "in %d of 6 the call the host crashed on had not returned and "
      "told of the death itself\n",
      toldByTheFaultingCall);
  std::printf("passed\n");
  return 0;
}
