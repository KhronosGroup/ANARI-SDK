// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Device death while a caller waits for a reply: the host crashes with the
// answer to anariGetProperty, anariMapFrame or an introspection query still
// owed. The caller is woken and returns as if from a dead device.

#include "DeathTest.h"

using namespace guard_test;

// One device, dead at 'fault', with the caller inside 'blockedCall'.
template <typename F>
static void run(
    ANARILibrary library, Seen &seen, const char *fault, F &&blockedCall)
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
  CHECK(seen.fatalCount() == 0);

  blockedCall(scene);

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

  // Before the wrapped device is asked.
  run(library, seen, "getProperty:1:before:segv", [](Scene &scene) {
    float duration = -1.f;
    CHECK(anariGetProperty(scene.device,
              scene.frame,
              "duration",
              ANARI_FLOAT32,
              &duration,
              sizeof(duration),
              ANARI_WAIT)
        == 0);
    CHECK(duration == -1.f);
  });

  // After it has answered, with the reply not yet sent.
  run(library, seen, "getProperty:1:after:segv", [](Scene &scene) {
    float duration = -1.f;
    CHECK(anariGetProperty(scene.device,
              scene.frame,
              "duration",
              ANARI_FLOAT32,
              &duration,
              sizeof(duration),
              ANARI_WAIT)
        == 0);
    CHECK(duration == -1.f);
  });

  for (const char *fault :
      {"mapFrame:1:before:segv", "mapFrame:1:after:segv"}) {
    run(library, seen, fault, [](Scene &scene) {
      uint32_t width = 77, height = 77;
      ANARIDataType pixelType = ANARI_FLOAT32;
      CHECK(anariMapFrame(scene.device,
                scene.frame,
                "channel.color",
                &width,
                &height,
                &pixelType)
          == nullptr);
      CHECK(width == 0 && height == 0 && pixelType == ANARI_UNKNOWN);
      anariUnmapFrame(scene.device, scene.frame, "channel.color");
    });
  }

  run(library, seen, "getObjectSubtypes:1:after:segv", [](Scene &scene) {
    CHECK(anariGetObjectSubtypes(scene.device, ANARI_RENDERER) == nullptr);
  });

  anariUnloadLibrary(library);
  std::printf("passed\n");
  return 0;
}
