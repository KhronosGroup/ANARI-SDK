// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Device death with regions mapped: memory the application was given in a
// region (a managed array, a parameter array mapping, a mapped frame) stays
// usable after the host is gone, and goes when the application lets go.

#include "TestSupport.h"

#include <anari/anari.h>

#include <atomic>
#include <cstring>
#include <vector>

#include <unistd.h>

using namespace guard_test;

static std::atomic<int> fatal{0};

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
}

static std::atomic<int> deleterRuns{0};
static void onDelete(const void *, const void *)
{
  deleterRuns++;
}

int main()
{
  ANARILibrary library = anariLoadLibrary("guard", onStatus, nullptr);
  CHECK(library);
  ANARIDevice d = anariNewDevice(library, "default");
  CHECK(d);
  const pid_t self = getpid();

  const uint64_t count = 1 << 18; // floats: 1 MB, the array ceiling
  const uint64_t few = 1 << 12; // floats: 16 KB, far below it

  ANARIArray1D managed =
      anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_FLOAT32, count);
  auto *managedMemory = static_cast<float *>(anariMapArray(d, managed));
  CHECK(managedMemory);
  for (uint64_t i = 0; i < count; i++)
    managedMemory[i] = float(i);
  // Left mapped across the death.

  std::vector<float> appMemory(count, 1.f);
  ANARIArray1D shared = anariNewArray1D(
      d, appMemory.data(), onDelete, nullptr, ANARI_FLOAT32, count);

  // Below the ceiling: a managed array that has its region, having been
  // mapped a second time, and is left mapped across the death; one that was
  // only filled, and a shared array, neither of which has a region yet.
  auto fillSmall = [&](ANARIArray1D array, float value) {
    auto *memory = static_cast<float *>(anariMapArray(d, array));
    CHECK(memory);
    for (uint64_t i = 0; i < few; i++)
      memory[i] = value + float(i);
    anariUnmapArray(d, array);
  };
  ANARIArray1D rewritten =
      anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_FLOAT32, few);
  fillSmall(rewritten, 100.f);
  auto *rewrittenMemory = static_cast<float *>(anariMapArray(d, rewritten));
  CHECK(rewrittenMemory && rewrittenMemory[few - 1] == 100.f + float(few - 1));
  ANARIArray1D filled =
      anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_FLOAT32, few);
  fillSmall(filled, 200.f);
  std::vector<float> smallMemory(few, 3.f);
  ANARIArray1D smallShared = anariNewArray1D(
      d, smallMemory.data(), nullptr, nullptr, ANARI_FLOAT32, few);

  ANARIGeometry geometry = anariNewGeometry(d, "triangle");
  uint64_t stride = 0;
  auto *parameterMemory = static_cast<float *>(anariMapParameterArray1D(
      d, geometry, "vertex.position", ANARI_FLOAT32_VEC3, count / 2, &stride));
  CHECK(parameterMemory);
  // Left mapped across the death too.

  ANARICamera camera = anariNewCamera(d, "perspective");
  anariCommitParameters(d, camera);
  ANARIRenderer renderer = anariNewRenderer(d, "default");
  anariCommitParameters(d, renderer);
  ANARIWorld world = anariNewWorld(d);
  anariCommitParameters(d, world);
  ANARIFrame frame = anariNewFrame(d);
  const uint32_t size[2] = {640, 480};
  const ANARIDataType colorType = ANARI_UFIXED8_RGBA_SRGB;
  anariSetParameter(d, frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(d, frame, "channel.color", ANARI_DATA_TYPE, &colorType);
  anariSetParameter(d, frame, "camera", ANARI_CAMERA, &camera);
  anariSetParameter(d, frame, "renderer", ANARI_RENDERER, &renderer);
  anariSetParameter(d, frame, "world", ANARI_WORLD, &world);
  anariCommitParameters(d, frame);
  anariRenderFrame(d, frame);
  CHECK(anariFrameReady(d, frame, ANARI_WAIT) == 1);

  uint32_t width = 0, height = 0;
  ANARIDataType pixelType = ANARI_UNKNOWN;
  auto *pixels = static_cast<const uint8_t *>(
      anariMapFrame(d, frame, "channel.color", &width, &height, &pixelType));
  CHECK(pixels && width == size[0] && height == size[1]);
  const size_t pixelBytes = size_t(width) * height * 4;
  const std::vector<uint8_t> before(pixels, pixels + pixelBytes);

  // The managed array, the shared array's copy, the parameter mapping, the
  // frame's channel, and the small managed array that was mapped again.
  CHECK(countRegions(self) == 5);

  // Device death.
  const pid_t host = findChild(self, "anariGuardHost");
  CHECK(host > 0);
  CHECK(kill(host, SIGKILL) == 0);
  CHECK(waitFor([&] { return !processExists(host); }));

  // Everything mapped before the death is still there, to read and to write.
  CHECK(countRegions(self) == 5);
  for (uint64_t i = 0; i < few; i++) {
    CHECK(rewrittenMemory[i] == 100.f + float(i));
    rewrittenMemory[i] = -2.f;
  }
  CHECK(std::memcmp(pixels, before.data(), pixelBytes) == 0);
  for (uint64_t i = 0; i < count; i++) {
    CHECK(managedMemory[i] == float(i));
    managedMemory[i] = -1.f;
  }
  std::memset(parameterMemory, 0, (count / 2) * 12);

  // Unmapping is local and does nothing more, except that the first call
  // made on the dead device is the one that tells of the death.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(fatal == 0);
  anariUnmapArray(d, managed);
  CHECK(fatal == 1);
  anariUnmapParameterArray(d, geometry, "vertex.position");
  anariUnmapFrame(d, frame, "channel.color");
  anariUnmapArray(d, rewritten);
  CHECK(countRegions(self) == 4); // the parameter mapping's went

  // The frame's last pixels can no longer be asked for, the arrays can.
  CHECK(anariMapFrame(d, frame, "channel.color", &width, &height, &pixelType)
      == nullptr);
  CHECK(anariMapArray(d, managed) == managedMemory);
  CHECK(managedMemory[count - 1] == -1.f);
  anariUnmapArray(d, managed);
  CHECK(anariMapArray(d, shared) == appMemory.data());
  anariUnmapArray(d, shared);
  CHECK(anariMapArray(d, rewritten) == rewrittenMemory);
  CHECK(rewrittenMemory[few - 1] == -2.f);
  anariUnmapArray(d, rewritten);

  // Arrays below the ceiling that had no region get none now, written again
  // or not: there is no host to read one. They hold what they held.
  auto *filledMemory = static_cast<float *>(anariMapArray(d, filled));
  CHECK(filledMemory && filledMemory[few - 1] == 200.f + float(few - 1));
  filledMemory[0] = -3.f;
  anariUnmapArray(d, filled);
  CHECK(anariMapArray(d, filled) == filledMemory && filledMemory[0] == -3.f);
  anariUnmapArray(d, filled);
  CHECK(anariMapArray(d, smallShared) == smallMemory.data());
  smallMemory[0] = 4.f;
  anariUnmapArray(d, smallShared);
  CHECK(countRegions(self) == 4);

  // New arrays and mappings on the dead device still hand out memory.
  ANARIArray1D late =
      anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_FLOAT32, count);
  auto *lateMemory = static_cast<float *>(anariMapArray(d, late));
  CHECK(lateMemory);
  std::memset(lateMemory, 0, count * sizeof(float));
  anariUnmapArray(d, late);
  auto *lateMapping = static_cast<float *>(anariMapParameterArray1D(
      d, geometry, "vertex.color", ANARI_FLOAT32_VEC4, count / 4, &stride));
  CHECK(lateMapping && stride == 16);
  std::memset(lateMapping, 0, count * 4);
  anariUnmapParameterArray(d, geometry, "vertex.color");
  // A shared array made now needs no region: nothing will read a copy.
  ANARIArray1D lateShared = anariNewArray1D(
      d, appMemory.data(), nullptr, nullptr, ANARI_FLOAT32, count);
  CHECK(countRegions(self) == 5);

  // Releasing gives every region back.
  anariRelease(d, smallShared);
  anariRelease(d, filled);
  anariRelease(d, rewritten);
  anariRelease(d, lateShared);
  anariRelease(d, late);
  anariRelease(d, managed);
  CHECK(deleterRuns == 0);
  anariRelease(d, shared);
  CHECK(deleterRuns == 1);
  CHECK(countRegions(self) == 1);
  anariRelease(d, frame);
  CHECK(countRegions(self) == 0);

  anariRelease(d, geometry);
  anariRelease(d, world);
  anariRelease(d, renderer);
  anariRelease(d, camera);
  anariRelease(d, d);
  anariUnloadLibrary(library);

  CHECK(fatal == 1);
  std::printf("passed\n");
  return 0;
}
