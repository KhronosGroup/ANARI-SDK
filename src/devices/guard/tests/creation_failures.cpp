// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// A device that cannot be created is not a dead device: anariNewDevice
// returns NULL, the reason is an error from the library, and no host is left
// running. A failed extension query is an empty list and a warning. And an
// application that exits without releasing its device leaves no host behind.

#include "DeathTest.h"
#include "Recording.h"

#include <sys/wait.h>

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/lsan_interface.h>
#endif

using namespace guard_test;

static bool anyHost()
{
  return findChild(getpid(), TEST_HOST) > 0
      || findChild(getpid(), "anariGuardHost") > 0;
}

// Exactly one message containing 'reason', of this severity, from the
// library and naming no device.
static void checkReason(
    StatusLog &status, const char *reason, ANARIStatusSeverity severity)
{
  const auto found = status.matching(reason);
  if (found.size() != 1) {
    std::fprintf(stderr,
        "FAILED: %zu messages contain '%s'; all of them:\n",
        found.size(),
        reason);
    std::lock_guard<std::mutex> lock(status.mutex);
    for (auto &m : status.messages)
      std::fprintf(stderr, "  %s\n", m.text.c_str());
    std::exit(1);
  }
  CHECK(found[0].severity == severity);
  CHECK(found[0].device == nullptr);
  CHECK(found[0].source == nullptr);
  CHECK(found[0].sourceType == ANARI_LIBRARY);
  // Whatever else was said, nothing claims a device died.
  std::lock_guard<std::mutex> lock(status.mutex);
  for (auto &m : status.messages) {
    CHECK(m.severity != ANARI_SEVERITY_FATAL_ERROR);
    CHECK(m.device == nullptr);
  }
}

static void creationFails(ANARILibrary library,
    StatusLog &status,
    const char *subtype,
    const char *reason)
{
  {
    std::lock_guard<std::mutex> lock(status.mutex);
    status.messages.clear();
  }
  CHECK(anariNewDevice(library, subtype) == nullptr);
  checkReason(status, reason, ANARI_SEVERITY_ERROR);
  CHECK(!anyHost());
}

static void queryFails(ANARILibrary library,
    StatusLog &status,
    const char *subtype,
    const char *reason)
{
  {
    std::lock_guard<std::mutex> lock(status.mutex);
    status.messages.clear();
  }
  const char **list = anariGetDeviceExtensions(library, subtype);
  CHECK(list && list[0] == nullptr);
  checkReason(status, reason, ANARI_SEVERITY_WARNING);
  CHECK(!anyHost());
}

// In a process of its own: a device is created and used, and the process
// exits with the device and the library still held.
static void exitWithoutRelease()
{
  int report[2];
  CHECK(pipe(report) == 0);
  const pid_t application = fork();
  CHECK(application >= 0);
  if (application == 0) {
#if defined(__SANITIZE_ADDRESS__)
    // Leaving everything behind is what this application is for.
    __lsan_disable();
#endif
    ANARILibrary library = anariLoadLibrary("guard", nullptr, nullptr);
    ANARIDevice device = library ? anariNewDevice(library, "default") : nullptr;
    pid_t host = -1;
    if (device) {
      Scene scene(device);
      anariRenderFrame(device, scene.frame);
      anariFrameReady(device, scene.frame, ANARI_WAIT);
      host = testHost();
    }
    if (write(report[1], &host, sizeof(host)) != sizeof(host))
      _exit(3);
    std::exit(0);
  }

  close(report[1]);
  pid_t host = -1;
  CHECK(read(report[0], &host, sizeof(host)) == sizeof(host));
  close(report[0]);
  CHECK(host > 0);

  int status = 0;
  CHECK(waitpid(application, &status, 0) == application);
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  CHECK(waitFor([&] { return !processExists(host); }));
}

int main()
{
  const std::string testHostPath = std::getenv("ANARI_GUARD_HOST");
  const std::string wrapped = std::getenv("ANARI_GUARD_WRAPPED_DEVICE");
  clearFault();

  // Before this process has any threads of its own.
  exitWithoutRelease();

  StatusLog status;
  ANARILibrary library =
      anariLoadLibrary("guard", StatusLog::callback, &status);
  CHECK(library);

  // The host executable is not there.
  setenv("ANARI_GUARD_HOST", "/nonexistent/anariGuardHost", 1);
  creationFails(library, status, "default", "cannot be executed");
  queryFails(library, status, "default", "cannot be executed");
  setenv("ANARI_GUARD_HOST", testHostPath.c_str(), 1);

  // The wrapped library cannot be loaded.
  creationFails(library,
      status,
      "no_such_library",
      "cannot load ANARI library 'no_such_library'");
  queryFails(library,
      status,
      "no_such_library",
      "cannot load ANARI library 'no_such_library'");

  // No wrapped device is named anywhere.
  unsetenv("ANARI_GUARD_WRAPPED_DEVICE");
  creationFails(library, status, "default", "no wrapped device named");
  queryFails(library, status, "default", "no wrapped device named");
  setenv("ANARI_GUARD_WRAPPED_DEVICE", wrapped.c_str(), 1);

  // The host dies before it has said hello.
  setFault("hello:1:before:segv");
  creationFails(library, status, "default", "ended before saying hello");
  queryFails(library, status, "default", "ended before saying hello");
  clearFault();

  // None of which stops the next device from working.
  ANARIDevice device = anariNewDevice(library, "default");
  CHECK(device);
  const char **extensions = anariGetDeviceExtensions(library, "default");
  CHECK(extensions && extensions[0]);
  Scene scene(device);
  anariRenderFrame(device, scene.frame);
  CHECK(anariFrameReady(device, scene.frame, ANARI_WAIT) == 1);
  scene.release();
  anariRelease(device, device);
  anariUnloadLibrary(library);

  CHECK(!anyHost());
  std::printf("passed\n");
  return 0;
}
