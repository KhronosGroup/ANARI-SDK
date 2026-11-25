// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The adapter's traffic counters, read as properties of the device, and the
// two tuning variables. With no argument: the counters, at the default array
// ceiling. 'threshold' and 'probe' each set one variable and show that the
// guard device goes by it.

#include "Recording.h"
#include "TestSupport.h"

#include <anari/anari.h>

#include <chrono>
#include <cstring>
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

struct Counters
{
  uint64_t bytesSent{0};
  uint64_t bytesReceived{0};
  uint64_t flushes{0};
  uint64_t regionsCreated{0};
};

static uint64_t counter(ANARIDevice d, const char *name)
{
  uint64_t value = ~uint64_t(0);
  CHECK(anariGetProperty(
      d, d, name, ANARI_UINT64, &value, sizeof(value), ANARI_NO_WAIT));
  return value;
}

static Counters read(ANARIDevice d)
{
  Counters c;
  c.bytesSent = counter(d, "guard.bytesSent");
  c.bytesReceived = counter(d, "guard.bytesReceived");
  c.flushes = counter(d, "guard.flushes");
  c.regionsCreated = counter(d, "guard.regionsCreated");
  return c;
}

// How many regions an array of 'bytes' bytes makes on its way to the host.
static uint64_t regionsFor(ANARIDevice d, size_t bytes)
{
  const uint64_t before = counter(d, "guard.regionsCreated");
  std::vector<uint8_t> data(bytes, 7);
  ANARIArray1D array =
      anariNewArray1D(d, data.data(), nullptr, nullptr, ANARI_UINT8, bytes);
  CHECK(array);
  anariRelease(d, array);
  return counter(d, "guard.regionsCreated") - before;
}

static ANARIDevice newGuardDevice(ANARILibrary &library)
{
  library = anariLoadLibrary("guard", onStatus, nullptr);
  CHECK(library);
  ANARIDevice d = anariNewDevice(library, "default");
  CHECK(d);
  return d;
}

static void counters()
{
  const std::string log = useRecordingDevice();
  ANARILibrary library = nullptr;
  ANARIDevice d = newGuardDevice(library);

  // Creating the device was one message out and one reply back.
  const Counters start = read(d);
  CHECK(start.flushes == 1);
  CHECK(start.bytesSent > 8);
  CHECK(start.bytesReceived > 8);
  CHECK(start.regionsCreated == 0);

  // Reading the counters sends nothing.
  const Counters again = read(d);
  CHECK(again.flushes == start.flushes);
  CHECK(again.bytesSent == start.bytesSent);

  // Pipelined calls wait in the adapter.
  ANARICamera camera = anariNewCamera(d, "perspective");
  const float fovy = 1.f;
  for (int i = 0; i < 10; i++)
    anariSetParameter(d, camera, "fovy", ANARI_FLOAT32, &fovy);
  anariCommitParameters(d, camera);
  const Counters pipelined = read(d);
  CHECK(pipelined.flushes == start.flushes);
  CHECK(pipelined.bytesSent == start.bytesSent);

  // A call that returns a value sends them all at once, with itself, and
  // reads a reply: an 8-byte header, the flag and the value.
  int32_t answer = 0;
  CHECK(anariGetProperty(d,
      camera,
      "answer",
      ANARI_INT32,
      &answer,
      sizeof(answer),
      ANARI_NO_WAIT));
  const Counters called = read(d);
  CHECK(called.flushes == pipelined.flushes + 1);
  // Thirteen messages, each with its header
  CHECK(called.bytesSent >= pipelined.bytesSent + 13 * 8);
  CHECK(called.bytesReceived >= pipelined.bytesReceived + 8 + 4 + 4);

  // The default array ceiling is 1 MB: an array of that size or more crosses
  // in a region from the start, whose descriptor is a message sent on its own.
  CHECK(regionsFor(d, 1024 * 1024 - 1) == 0);
  CHECK(regionsFor(d, 1024 * 1024) == 1);
  const Counters before = read(d);
  CHECK(regionsFor(d, 4 * 1024 * 1024) == 1);
  const Counters after = read(d);
  CHECK(after.flushes > before.flushes);
  // The megabytes themselves went nowhere near the channel.
  CHECK(after.bytesSent - before.bytesSent < 1024);

  // Below the ceiling a shared array crosses inline when it is made, and is
  // given a region, which it keeps, the first time it is written again.
  {
    const size_t bytes = 128 * 1024;
    std::vector<uint8_t> data(bytes, 7);
    const uint64_t regions = counter(d, "guard.regionsCreated");
    ANARIArray1D array =
        anariNewArray1D(d, data.data(), nullptr, nullptr, ANARI_UINT8, bytes);
    CHECK(counter(d, "guard.regionsCreated") == regions);
    for (int write = 2; write <= 4; write++) {
      CHECK(anariMapArray(d, array) == data.data());
      data[0] = uint8_t(write);
      const Counters unmapping = read(d);
      anariUnmapArray(d, array);
      const Counters unmapped = read(d);
      CHECK(unmapped.regionsCreated == regions + 1);
      // Once it has its region, its contents no longer go through the channel.
      if (write > 2)
        CHECK(unmapped.bytesSent - unmapping.bytesSent < 1024);
    }
    anariRelease(d, array);
  }

  // A managed array below the ceiling arrives empty, so its first mapping is
  // the fill, which crosses inline. Mapped a second time it is given a
  // region, with the contents it had, and keeps it.
  {
    const size_t bytes = 128 * 1024;
    const uint64_t regions = counter(d, "guard.regionsCreated");
    ANARIArray1D array =
        anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_UINT8, bytes);
    auto *first = static_cast<uint8_t *>(anariMapArray(d, array));
    CHECK(first);
    std::memset(first, 7, bytes);
    anariUnmapArray(d, array);
    CHECK(counter(d, "guard.regionsCreated") == regions);
    for (int write = 2; write <= 4; write++) {
      auto *mapped = static_cast<uint8_t *>(anariMapArray(d, array));
      CHECK(mapped);
      CHECK(counter(d, "guard.regionsCreated") == regions + 1);
      CHECK(
          mapped[0] == (write == 2 ? 7 : write - 1) && mapped[bytes - 1] == 7);
      mapped[0] = uint8_t(write);
      const Counters unmapping = read(d);
      anariUnmapArray(d, array);
      const Counters unmapped = read(d);
      CHECK(unmapped.regionsCreated == regions + 1);
      CHECK(unmapped.bytesSent - unmapping.bytesSent < 1024);
    }
    anariRelease(d, array);
  }

  // A property the guard device does not have is still the wrapped device's
  // to answer.
  uint64_t value = 0;
  CHECK(!anariGetProperty(d,
      d,
      "guard.noSuchCounter",
      ANARI_UINT64,
      &value,
      sizeof(value),
      ANARI_NO_WAIT));
  // And a counter asked for as the wrong type is not answered.
  int32_t narrow = 0;
  CHECK(!anariGetProperty(d,
      d,
      "guard.flushes",
      ANARI_INT32,
      &narrow,
      sizeof(narrow),
      ANARI_NO_WAIT));

  anariRelease(d, camera);
  anariRelease(d, d);
  anariUnloadLibrary(library);
  unlink(log.c_str());
}

// ANARI_GUARD_INLINE_THRESHOLD moves the array ceiling, and nothing else.
static void inlineThreshold()
{
  setenv("ANARI_GUARD_INLINE_THRESHOLD", "4096", 1);
  const std::string log = useRecordingDevice();
  ANARILibrary library = nullptr;
  ANARIDevice d = newGuardDevice(library);

  CHECK(regionsFor(d, 4095) == 0);
  CHECK(regionsFor(d, 4096) == 1);

  // A frame channel mapped from the host does not go by it: one comes back
  // in a region from 16 KB, whatever the ceiling. 40 by 30 pixels of 4 bytes
  // are 4,800 bytes, above this ceiling, and 80 by 60 are 19,200.
  ANARIFrame frame = anariNewFrame(d);
  auto regionsToMap = [&](uint32_t width, uint32_t height) {
    const uint32_t size[2] = {width, height};
    anariSetParameter(d, frame, "size", ANARI_UINT32_VEC2, size);
    anariCommitParameters(d, frame);
    anariRenderFrame(d, frame);
    CHECK(anariFrameReady(d, frame, ANARI_WAIT) == 1);
    const uint64_t before = counter(d, "guard.regionsCreated");
    uint32_t w = 0, h = 0;
    ANARIDataType type = ANARI_UNKNOWN;
    const void *pixels =
        anariMapFrame(d, frame, "channel.color", &w, &h, &type);
    CHECK(pixels && w == width && h == height);
    anariUnmapFrame(d, frame, "channel.color");
    return counter(d, "guard.regionsCreated") - before;
  };
  CHECK(regionsToMap(40, 30) == 0);
  CHECK(regionsToMap(80, 60) == 1);

  anariRelease(d, frame);
  anariRelease(d, d);
  anariUnloadLibrary(library);
  unlink(log.c_str());
}

// ANARI_GUARD_PROBE_PERIOD_US is how often the host asks a wrapped device
// that does not call back whether its render is over. The recording device's
// 'slow' render takes 200 ms; asked only every 700 ms, the host finds out at
// the first asking after that.
static void probePeriod()
{
  setenv("ANARI_GUARD_PROBE_PERIOD_US", "700000", 1);
  const std::string log = useRecordingDevice("slow");
  ANARILibrary library = nullptr;
  ANARIDevice d = newGuardDevice(library);

  ANARIFrame frame = anariNewFrame(d);
  anariCommitParameters(d, frame);
  const auto start = std::chrono::steady_clock::now();
  anariRenderFrame(d, frame);
  CHECK(anariFrameReady(d, frame, ANARI_WAIT) == 1);
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();
  std::printf("a 200 ms render, asked about every 700 ms, took %.0f ms\n",
      seconds * 1e3);
  CHECK(seconds >= 0.6);

  anariRelease(d, frame);
  anariRelease(d, d);
  anariUnloadLibrary(library);
  unlink(log.c_str());
}

int main(int argc, char *argv[])
{
  if (argc > 1 && std::strcmp(argv[1], "threshold") == 0)
    inlineThreshold();
  else if (argc > 1 && std::strcmp(argv[1], "probe") == 0)
    probePeriod();
  else
    counters();
  std::printf("passed\n");
  return 0;
}
