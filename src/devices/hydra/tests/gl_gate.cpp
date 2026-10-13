// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Test launcher for tests that need GL (Storm): `gl_gate <test> [args...]`
// runs the test unless this machine has no GL context at all (no EGL and no X
// display), in which case it exits with kSkip, which ctest reports as
// "Skipped" (SKIP_RETURN_CODE). It asks the device itself: a storm device
// whose creation is refused with "GL context none" means no context could be
// made. Any other outcome, including other creation errors, runs the test, so
// real failures still fail.

#include <anari/anari.h>

#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr int kSkip = 77;
bool g_noGL = false;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_ERROR && message
      && std::strstr(message, "with GL context none"))
    g_noGL = true;
}

bool glAvailable()
{
  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  if (!lib)
    return true; // let the test report it
  ANARIDevice dev = anariNewDevice(lib, "storm");
  if (dev)
    anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  return dev || !g_noGL;
}

} // namespace

int main(int argc, char **argv)
{
  if (argc < 2) {
    std::fprintf(stderr, "usage: gl_gate <test> [args...]\n");
    return 2;
  }
  if (!glAvailable()) {
    std::printf(
        "SKIP: no GL context (no EGL device and no X display); "
        "storm needs one\n");
    return kSkip;
  }
  execv(argv[1], argv + 1);
  std::perror("gl_gate: execv");
  return 2;
}
