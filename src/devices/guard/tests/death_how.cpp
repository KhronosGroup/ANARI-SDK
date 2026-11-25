// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// guard_death_how <abort|exit|kill|segv|unreachable> <text>
//
// The fatal message says how the host ended: <text> is what it must contain
// for a host that ended by <how>, once with the application idle (told at
// its next call) and once with a caller waiting for a reply.

#include "DeathTest.h"

using namespace guard_test;

int main(int argc, char **argv)
{
  CHECK(argc == 3);
  const std::string how = argv[1];
  const std::string text = argv[2];

  Seen seen;
  ANARILibrary library = anariLoadLibrary("guard", Seen::callback, &seen);
  CHECK(library);

  for (const char *call : {"discardFrame", "getProperty"}) {
    seen.reset();
    setFault((std::string(call) + ":1:before:" + how).c_str());
    ANARIDevice device = anariNewDevice(library, "default");
    CHECK(device);
    const pid_t host = testHost();
    CHECK(host > 0);

    Scene scene(device);
    anariRenderFrame(device, scene.frame);
    CHECK(anariFrameReady(device, scene.frame, ANARI_WAIT) == 1);
    CHECK(seen.fatalCount() == 0);

    float duration = -1.f;
    if (std::string(call) == "discardFrame") {
      anariDiscardFrame(device, scene.frame);
      if (checkNotTold(seen, host))
        anariCommitParameters(device, scene.camera);
    } else {
      CHECK(anariGetProperty(device,
                scene.frame,
                "duration",
                ANARI_FLOAT32,
                &duration,
                sizeof(duration),
                ANARI_WAIT)
          == 0);
    }

    checkDeathMessage(seen, device, text);
    CHECK(!processExists(host));
    checkDeadDevice(scene);

    scene.release();
    anariRelease(device, device);
    CHECK(seen.fatal == 1);
    CHECK(seen.afterDeath == 0);
  }

  anariUnloadLibrary(library);
  std::printf("passed\n");
  return 0;
}
