// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// An application may throw from its fatal status callback, or end the
// process there. The death message is delivered under the application's own
// call, so what is thrown reaches the caller of that call, and exit() runs on
// the application's thread.
//
// The calls that throw are made on the device itself (anari::DeviceImpl),
// not through the C API: libanari as it is usually built catches whatever a
// device call throws and terminates, for any device.

#include "DeathTest.h"

#include <anari/backend/DeviceImpl.h>

#include <stdexcept>

#include <sys/wait.h>

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/lsan_interface.h>
#endif

using namespace guard_test;

struct DeviceLost : public std::runtime_error
{
  using std::runtime_error::runtime_error;
};

static int thrown = 0;

static void throwOnFatal(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity == ANARI_SEVERITY_FATAL_ERROR) {
    thrown++;
    throw DeviceLost(message);
  }
}

static void exitOnFatal(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *)
{
  if (severity == ANARI_SEVERITY_FATAL_ERROR)
    std::exit(7);
}

static bool saysSignal11(const DeviceLost &e)
{
  return std::string(e.what()).find("killed by signal 11") != std::string::npos;
}

// Thrown under a call that was waiting for a reply when the host crashed.
static void thrownToABlockedCaller(ANARILibrary library)
{
  thrown = 0;
  setFault("getProperty:1:before:segv");
  ANARIDevice device = anariNewDevice(library, "default");
  CHECK(device);
  auto &impl = *reinterpret_cast<anari::DeviceImpl *>(device);
  Scene scene(device);

  bool caught = false;
  float duration = -1.f;
  try {
    impl.getProperty(scene.frame,
        "duration",
        ANARI_FLOAT32,
        &duration,
        sizeof(duration),
        ANARI_WAIT);
  } catch (const DeviceLost &e) {
    caught = saysSignal11(e);
  }
  CHECK(caught && thrown == 1);
  CHECK(duration == -1.f);

  // The device is a dead device like any other, and nothing is thrown twice.
  checkDeadDevice(scene);
  scene.release();
  anariRelease(device, device);
  CHECK(thrown == 1);
  CHECK(testHost() == -1);
}

// Thrown under the first call made after the device died while idle. The
// host is killed from outside, so that no call is under way when it dies.
static void thrownToTheNextCaller(ANARILibrary library)
{
  thrown = 0;
  clearFault();
  ANARIDevice device = anariNewDevice(library, "default");
  CHECK(device);
  auto &impl = *reinterpret_cast<anari::DeviceImpl *>(device);
  const pid_t host = testHost();
  CHECK(host > 0);
  Scene scene(device);
  CHECK(kill(host, SIGKILL) == 0);
  CHECK(waitFor([&] { return !processExists(host); }));
  CHECK(thrown == 0);

  bool caught = false;
  try {
    impl.commitParameters(scene.camera);
  } catch (const DeviceLost &e) {
    caught =
        std::string(e.what()).find("killed by signal 9") != std::string::npos;
  }
  CHECK(caught && thrown == 1);

  checkDeadDevice(scene);
  scene.release();
  anariRelease(device, device);
  CHECK(thrown == 1);
}

// Thrown under the device's last release, which is finished all the same.
static void thrownToTheLastRelease(ANARILibrary library)
{
  thrown = 0;
  setFault("releaseDevice:1:before:segv");
  ANARIDevice device = anariNewDevice(library, "default");
  CHECK(device);
  auto &impl = *reinterpret_cast<anari::DeviceImpl *>(device);
  const pid_t host = testHost();
  CHECK(host > 0);
  Scene scene(device);
  scene.release();

  bool caught = false;
  try {
    impl.release(device);
  } catch (const DeviceLost &e) {
    caught = saysSignal11(e);
  }
  CHECK(caught && thrown == 1);
  CHECK(!processExists(host));
  CHECK(testHost() == -1);
}

// exit() from the callback, under a call that was waiting when the host
// crashed with a frame in flight: the process ends with the code it chose,
// with the adapter's threads still there.
static void exitsFromTheCallback()
{
  const pid_t child = fork();
  CHECK(child >= 0);
  if (child == 0) {
#if defined(__SANITIZE_ADDRESS__)
    // An application that exits from its callback leaves everything behind.
    __lsan_disable();
#endif
    ANARILibrary library = anariLoadLibrary("guard", exitOnFatal, nullptr);
    CHECK(library);
    setFault("renderFrame:1:before:segv");
    ANARIDevice device = anariNewDevice(library, "default");
    CHECK(device);
    Scene scene(device);
    anariRenderFrame(device, scene.frame);
    anariFrameReady(device, scene.frame, ANARI_WAIT);
    _exit(1); // not reached: the callback ended the process
  }
  int status = 0;
  CHECK(waitpid(child, &status, 0) == child);
  if (WIFSIGNALED(status))
    std::fprintf(stderr, "ended by signal %d\n", WTERMSIG(status));
  CHECK(WIFEXITED(status));
  CHECK(WEXITSTATUS(status) == 7);
}

int main()
{
  exitsFromTheCallback();

  ANARILibrary library = anariLoadLibrary("guard", throwOnFatal, nullptr);
  CHECK(library);
  thrownToABlockedCaller(library);
  thrownToTheNextCaller(library);
  thrownToTheLastRelease(library);
  anariUnloadLibrary(library);
  std::printf("passed\n");
  return 0;
}
