// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// An application with no status callback anywhere at device death is told
// by the call that commits one: the message is held, not lost.

#include "TestSupport.h"

#include <anari/anari.h>

#include <atomic>
#include <cstring>

#include <unistd.h>

using namespace guard_test;

static std::atomic<int> fatal{0};
static std::atomic<int> other{0};

static void onStatus(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *)
{
  if (severity == ANARI_SEVERITY_FATAL_ERROR)
    fatal++;
  else if (severity <= ANARI_SEVERITY_WARNING)
    other++;
}

int main()
{
  ANARILibrary library = anariLoadLibrary("guard", nullptr, nullptr);
  CHECK(library);
  ANARIDevice device = anariNewDevice(library, "default");
  CHECK(device);

  // A round trip, so the host is known to be up and serving the device.
  CHECK(anariGetObjectSubtypes(device, ANARI_RENDERER));

  const pid_t host = findChild(getpid(), "anariGuardHost");
  CHECK(host > 0);
  CHECK(kill(host, SIGKILL) == 0);

  // The adapter reaps the host when it has seen the death.
  CHECK(waitFor([&] { return !processExists(host); }));
  CHECK(fatal == 0);

  // Setting the callback is not enough; committing it is.
  ANARIStatusCallback callback = onStatus;
  anariSetParameter(
      device, device, "statusCallback", ANARI_STATUS_CALLBACK, &callback);
  CHECK(anariGetObjectSubtypes(device, ANARI_RENDERER));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(fatal == 0);
  anariCommitParameters(device, device);
  CHECK(fatal == 1);

  // Once only, however often the device is committed afterwards.
  anariCommitParameters(device, device);
  anariRelease(device, device);
  anariUnloadLibrary(library);
  CHECK(fatal == 1);
  CHECK(other == 0);
  std::printf("passed\n");
  return 0;
}
