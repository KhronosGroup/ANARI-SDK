// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Frame completion: every render through the guard device ends, whether or
// not the wrapped device calls back for it, and the application's completion
// callback runs as often, and as early, as it does in-process.

#include "Recording.h"
#include "TestSupport.h"

#include <anari/anari.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

using namespace guard_test;

static void onStatus(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_WARNING)
    std::fprintf(stderr, "[status] %s\n", message);
}

static void onFrameCompleted(const void *userData, ANARIDevice, ANARIFrame)
{
  (*static_cast<std::atomic<int> *>(const_cast<void *>(userData)))++;
}

// Takes its time, and counts only once it is done.
struct SlowCallback
{
  std::atomic<int> begun{0};
  std::atomic<int> returned{0};
};

static void onFrameCompletedSlowly(
    const void *userData, ANARIDevice, ANARIFrame)
{
  auto &c = *static_cast<SlowCallback *>(const_cast<void *>(userData));
  c.begun++;
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  c.returned++;
}

// A frame of an empty world, with a completion callback that counts.
struct Target
{
  Target(ANARIDevice d,
      const void *userData,
      ANARIFrameCompletionCallback callback = onFrameCompleted)
      : d(d)
  {
    camera = anariNewCamera(d, "perspective");
    anariCommitParameters(d, camera);
    renderer = anariNewRenderer(d, "default");
    setBackground(0.f, 0.f, 0.f);
    world = anariNewWorld(d);
    anariCommitParameters(d, world);

    frame = anariNewFrame(d);
    const uint32_t size[2] = {64, 48};
    const ANARIDataType colorType = ANARI_UFIXED8_RGBA_SRGB;
    anariSetParameter(d, frame, "size", ANARI_UINT32_VEC2, size);
    anariSetParameter(d, frame, "channel.color", ANARI_DATA_TYPE, &colorType);
    anariSetParameter(d, frame, "camera", ANARI_CAMERA, &camera);
    anariSetParameter(d, frame, "renderer", ANARI_RENDERER, &renderer);
    anariSetParameter(d, frame, "world", ANARI_WORLD, &world);
    anariSetParameter(d,
        frame,
        "frameCompletionCallback",
        ANARI_FRAME_COMPLETION_CALLBACK,
        &callback);
    anariSetParameter(d,
        frame,
        "frameCompletionCallbackUserData",
        ANARI_VOID_POINTER,
        userData);
    anariCommitParameters(d, frame);
  }

  ~Target()
  {
    anariRelease(d, frame);
    anariRelease(d, world);
    anariRelease(d, renderer);
    anariRelease(d, camera);
  }

  void setBackground(float r, float g, float b)
  {
    const float color[4] = {r, g, b, 1.f};
    anariSetParameter(d, renderer, "background", ANARI_FLOAT32_VEC4, color);
    anariCommitParameters(d, renderer);
  }

  // The first pixel's four bytes.
  std::vector<uint8_t> firstPixel()
  {
    uint32_t w = 0, h = 0;
    ANARIDataType type = ANARI_UNKNOWN;
    auto *p = static_cast<const uint8_t *>(
        anariMapFrame(d, frame, "channel.color", &w, &h, &type));
    CHECK(p && w == 64 && h == 48);
    std::vector<uint8_t> pixel(p, p + 4);
    anariUnmapFrame(d, frame, "channel.color");
    return pixel;
  }

  ANARIDevice d;
  ANARICamera camera;
  ANARIRenderer renderer;
  ANARIWorld world;
  ANARIFrame frame;
};

// One frame rendered three times with nothing changed in between, waited for
// each time: how many callbacks there had been when each wait returned. A
// wrapped device may decline the second and third and never call back.
static std::vector<int> unchangedThrice(ANARIDevice d)
{
  std::atomic<int> callbacks{0};
  std::vector<int> seen;
  {
    Target t(d, &callbacks);
    for (int i = 0; i < 3; i++) {
      anariRenderFrame(d, t.frame);
      CHECK(anariFrameReady(d, t.frame, ANARI_WAIT) == 1);
      CHECK(anariFrameReady(d, t.frame, ANARI_NO_WAIT) == 1);
      seen.push_back(callbacks);
    }
  }
  return seen;
}

// A frame rendered twice with a change before each and no wait in between,
// over and over: the first pixel after each wait. The wait is for the second
// render, not the first, so the pixel is the second render's, and whatever
// callbacks the two renders bring have all returned by then. (How many that
// is varies from run to run in-process on helide, so it is not compared.)
static std::vector<std::vector<uint8_t>> twiceWithoutWaiting(
    ANARIDevice d, int rounds)
{
  std::atomic<int> callbacks{0};
  std::vector<std::vector<uint8_t>> seen;
  {
    Target t(d, &callbacks);
    for (int i = 0; i < rounds; i++) {
      t.setBackground(1.f, 0.f, 0.f);
      anariRenderFrame(d, t.frame);
      t.setBackground(0.f, 0.f, float(i + 1) / float(rounds));
      anariRenderFrame(d, t.frame);
      CHECK(anariFrameReady(d, t.frame, ANARI_WAIT) == 1);
      const int atWait = callbacks;
      seen.push_back(t.firstPixel());
      CHECK(callbacks == atWait);
    }
    CHECK(callbacks >= 1);
  }
  return seen;
}

// A second render asked for while the application is still inside its
// callback for the first: how many callbacks had returned when the wait
// returned. The first render's completion is by then known to be for a render
// that is no longer the latest, and must release nobody.
static int renderedAgainDuringCallback(ANARIDevice d)
{
  SlowCallback callbacks;
  Target t(d, &callbacks, onFrameCompletedSlowly);
  anariRenderFrame(d, t.frame);
  CHECK(waitFor([&] { return callbacks.begun == 1; }));
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  t.setBackground(0.f, 1.f, 0.f);
  anariRenderFrame(d, t.frame);
  CHECK(anariFrameReady(d, t.frame, ANARI_WAIT) == 1);
  return callbacks.returned;
}

// Frames released while their render is still going, one after another: the
// device carries on, and a frame made afterwards renders and completes.
static std::vector<uint8_t> releasedWhileRendering(ANARIDevice d)
{
  // In-process, helide calls back even after the frame is released, so what
  // the callback writes to has to outlive this function.
  static std::atomic<int> callbacks{0};
  for (int i = 0; i < 50; i++) {
    Target t(d, &callbacks);
    anariRenderFrame(d, t.frame);
  }
  Target t(d, &callbacks);
  t.setBackground(0.f, 0.f, 1.f);
  anariRenderFrame(d, t.frame);
  CHECK(anariFrameReady(d, t.frame, ANARI_WAIT) == 1);
  return t.firstPixel();
}

// A wrapped device without the completion callback extension: nothing tells
// the host that a render is over, so it has to keep asking. While the render
// goes on, other calls still reach the wrapped device and come back.
static void withoutCallbackExtension()
{
  const std::string log = useRecordingDevice("slow");
  ANARILibrary library = anariLoadLibrary("guard", onStatus, nullptr);
  CHECK(library);
  ANARIDevice d = anariNewDevice(library, "default");
  CHECK(d);

  std::atomic<int> callbacks{0};
  ANARIFrame frame = anariNewFrame(d);
  ANARIFrameCompletionCallback callback = onFrameCompleted;
  anariSetParameter(d,
      frame,
      "frameCompletionCallback",
      ANARI_FRAME_COMPLETION_CALLBACK,
      &callback);
  anariSetParameter(d,
      frame,
      "frameCompletionCallbackUserData",
      ANARI_VOID_POINTER,
      &callbacks);
  anariCommitParameters(d, frame);

  for (int i = 0; i < 3; i++) {
    anariRenderFrame(d, frame);
    int32_t answer = 0;
    CHECK(anariGetProperty(d,
        frame,
        "answer",
        ANARI_INT32,
        &answer,
        sizeof(answer),
        ANARI_NO_WAIT));
    CHECK(answer == 42);
    CHECK(anariFrameReady(d, frame, ANARI_NO_WAIT) == 0);
    CHECK(anariFrameReady(d, frame, ANARI_WAIT) == 1);
  }
  CHECK(callbacks == 0); // the wrapped device never called back

  anariRelease(d, frame);
  anariRelease(d, d);
  anariUnloadLibrary(library);
  unlink(log.c_str());
}

static void print(const char *what, const std::vector<int> &counts)
{
  std::printf("  %s:", what);
  for (int c : counts)
    std::printf(" %d", c);
  std::printf("\n");
  std::fflush(stdout);
}

int main(int argc, char *argv[])
{
  if (argc > 1 && std::strcmp(argv[1], "timeout") == 0) {
    withoutCallbackExtension();
    std::printf("passed\n");
    return 0;
  }

  const char *wrapped = std::getenv("ANARI_GUARD_WRAPPED_DEVICE");
  CHECK(wrapped);

  ANARILibrary directLibrary = anariLoadLibrary(wrapped, onStatus, nullptr);
  CHECK(directLibrary);
  ANARIDevice direct = anariNewDevice(directLibrary, "default");
  CHECK(direct);

  ANARILibrary guardLibrary = anariLoadLibrary("guard", onStatus, nullptr);
  CHECK(guardLibrary);
  ANARIDevice guarded = anariNewDevice(guardLibrary, "default");
  CHECK(guarded);

  {
    std::printf("an unchanged frame rendered three times\n");
    const auto expected = unchangedThrice(direct);
    print("in-process", expected);
    const auto actual = unchangedThrice(guarded);
    print("guard device", actual);
    // helide calls back once: it declines to render an unchanged frame again.
    // A device that renders it each time calls back three times.
    CHECK(expected.back() == (std::strcmp(wrapped, "helide") == 0 ? 1 : 3));
    CHECK(actual == expected);
  }

  {
    std::printf("two renders with no wait between\n");
    const int rounds = 50;
    const auto expected = twiceWithoutWaiting(direct, rounds);
    const auto actual = twiceWithoutWaiting(guarded, rounds);
    CHECK(expected.front() != expected.back());
    CHECK(actual == expected);
  }

  {
    std::printf("a second render asked for during the first's callback\n");
    const int expected = renderedAgainDuringCallback(direct);
    const int actual = renderedAgainDuringCallback(guarded);
    std::printf("  in-process: %d, guard device: %d\n", expected, actual);
    CHECK(expected == 2);
    CHECK(actual == expected);
  }

  {
    std::printf("frames released while rendering\n");
    const auto expected = releasedWhileRendering(direct);
    CHECK(releasedWhileRendering(guarded) == expected);
  }

  anariRelease(guarded, guarded);
  anariUnloadLibrary(guardLibrary);
  anariRelease(direct, direct);
  // Left loaded: unloading a device's library can take its threads' code
  // from under them (helide's TBB workers may still be starting).
  (void)directLibrary;
  std::printf("passed\n");
  return 0;
}
