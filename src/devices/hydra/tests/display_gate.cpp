// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Test launcher: `display_gate <test> [args...]` runs the test only when an X
// display can be opened, and otherwise exits with kSkip, which ctest reports
// as "Skipped" (SKIP_RETURN_CODE). Every device subtype, embree included,
// opens a GL context (GarchGLDebugWindow) when the device is created, and
// without a display that segfaults inside Garch with no output.
//
// libX11 is loaded at run time, so the tests don't link against it; if it
// can't be loaded, Garch can't open a window either, and the test skips.

#include <dlfcn.h>
#include <unistd.h>

#include <cstdio>

namespace {

constexpr int kSkip = 77;

bool displayAvailable()
{
  void *x11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
  if (!x11)
    return false;
  using OpenFn = void *(*)(const char *);
  using CloseFn = int (*)(void *);
  auto open = reinterpret_cast<OpenFn>(dlsym(x11, "XOpenDisplay"));
  auto close = reinterpret_cast<CloseFn>(dlsym(x11, "XCloseDisplay"));
  bool ok = false;
  if (open && close) {
    if (void *display = open(nullptr)) {
      close(display);
      ok = true;
    }
  }
  dlclose(x11);
  return ok;
}

} // namespace

int main(int argc, char **argv)
{
  if (argc < 2) {
    std::fprintf(stderr, "usage: display_gate <test> [args...]\n");
    return 2;
  }
  if (!displayAvailable()) {
    std::printf("SKIP: no X display (DISPLAY/XAUTHORITY); the hydra device "
                "needs a GL context\n");
    return kSkip;
  }
  execv(argv[1], argv + 1);
  std::perror("display_gate: execv");
  return 2;
}
