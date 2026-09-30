// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Regressions for helide frames released without waiting on them. A render
// holds a reference to its frame until it ends, so a frame the app releases
// after (or while) rendering, or from its own completion callback, must still
// be destroyed, not reported as leaked when the device is released.

#include "catch.hpp"
#include "helide_render_test.h"

// std
#include <atomic>
#include <chrono>
#include <thread>

namespace {

using namespace helide_test;
using namespace std::chrono_literals;

// Waits for 'frame's render to end by polling anariFrameReady() with
// ANARI_NO_WAIT, which (unlike a wait) leaves the frame as it is.
void pollUntilReady(anari::Device d, anari::Frame frame)
{
  while (!anariFrameReady(d, frame, ANARI_NO_WAIT))
    std::this_thread::sleep_for(1ms);
}

// Completion callback that releases its frame and counts its calls in the
// std::atomic<int> passed as its user pointer.
void releaseFromCallback(const void *userPtr, ANARIDevice d, ANARIFrame frame)
{
  anariRelease(d, frame);
  (*(std::atomic<int> *)userPtr)++;
}

} // namespace

SCENARIO("a helide frame released without waiting on it is not leaked",
    "[helide_frame_release]")
{
  StatusLog log;
  anari::Library lib = anari::loadLibrary("helide", collectStatus, &log);
  if (lib == nullptr) {
    WARN("helide library not available; skipping frame release test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");

  auto world = anari::newObject<anari::World>(d);
  anari::commitParameters(d, world);

  GIVEN("a frame")
  {
    auto frame = newFrame(d, world);

    WHEN("it is rendered, and released once the render ends")
    {
      anari::render(d, frame);
      pollUntilReady(d, frame);
      anari::release(d, frame);
    }

    WHEN(
        "it is rendered twice without a wait (the second render skipped as "
        "nothing changed), and released once the renders end")
    {
      anari::render(d, frame);
      anari::render(d, frame);
      pollUntilReady(d, frame);
      anari::release(d, frame);
    }

    WHEN("it is invalid, rendered, and released once the render ends")
    {
      anari::unsetParameter(d, frame, "world");
      anari::commitParameters(d, frame);
      anari::render(d, frame);
      pollUntilReady(d, frame);
      anari::release(d, frame);
      CHECK(log.errors.contains("skipping render of incomplete frame"));
    }

    WHEN("it is released while its render is in flight")
    {
      anari::render(d, frame);
      anari::release(d, frame);

      AND_WHEN("more frames are rendered and released at once")
      {
        for (int i = 0; i < 20; i++) {
          auto f = newFrame(d, world);
          anari::render(d, f);
          anari::release(d, f);
        }
      }
    }

    WHEN("it releases itself from its completion callback")
    {
      std::atomic<int> calls{0};
      anari::setParameter(d,
          frame,
          "frameCompletionCallback",
          (ANARIFrameCompletionCallback)releaseFromCallback);
      anari::setParameter(
          d, frame, "frameCompletionCallbackUserData", (void *)&calls);
      anari::commitParameters(d, frame);
      anari::render(d, frame);

      // Released by the callback: the frame may no longer be used here.
      while (calls == 0)
        std::this_thread::sleep_for(1ms);
      CHECK(calls == 1);

      AND_WHEN(
          "more frames of the empty world, whose renders end at once, "
          "release themselves from their callbacks")
      {
        std::atomic<int> moreCalls{0};
        for (int i = 0; i < 50; i++) {
          auto f = newFrame(d, world);
          anari::setParameter(d,
              f,
              "frameCompletionCallback",
              (ANARIFrameCompletionCallback)releaseFromCallback);
          anari::setParameter(
              d, f, "frameCompletionCallbackUserData", (void *)&moreCalls);
          anari::commitParameters(d, f);
          anari::render(d, f);
        }
        while (moreCalls < 50)
          std::this_thread::sleep_for(1ms);
      }
    }

    WHEN(
        "it is released by a thread holding a mapped array while its render "
        "is queued")
    {
      MappedArray mapped(d);
      anari::render(d, frame);
      anari::release(d, frame);
      mapped.unmap();
    }
  }

  // Every path above: releasing the device reports no leaked objects.
  anari::release(d, world);
  anari::release(d, d);
  CHECK_FALSE(log.warnings.contains("leaked"));
  anari::unloadLibrary(lib);
}
