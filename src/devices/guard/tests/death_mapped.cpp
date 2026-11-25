// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Device death while the application holds mapped memory: the host crashes
// inside anariMapFrame with another channel, a managed array and a parameter
// array already mapped. Every pointer the application was given stays valid
// until it unmaps or releases, whether the data was in a region or small
// enough to have travelled inline.

#include "DeathTest.h"

using namespace guard_test;

// A frame, an array and a parameter mapping large enough to be regions.
static void inRegions(ANARILibrary library, Seen &seen)
{
  seen.reset();
  setFault("mapFrame:2:after:segv");
  ANARIDevice d = anariNewDevice(library, "default");
  CHECK(d);
  const pid_t host = testHost();
  CHECK(host > 0);
  const pid_t self = getpid();

  const uint64_t count = 1 << 18; // floats: 1 MB, the array ceiling
  ANARIArray1D managed =
      anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_FLOAT32, count);
  auto *managedMemory = static_cast<float *>(anariMapArray(d, managed));
  CHECK(managedMemory);
  for (uint64_t i = 0; i < count; i++)
    managedMemory[i] = float(i);

  ANARIGeometry geometry = anariNewGeometry(d, "triangle");
  uint64_t stride = 0;
  auto *parameterMemory = static_cast<float *>(anariMapParameterArray1D(
      d, geometry, "vertex.position", ANARI_FLOAT32_VEC3, count / 2, &stride));
  CHECK(parameterMemory);

  Scene scene(d, 640, 480);
  anariRenderFrame(d, scene.frame);
  CHECK(anariFrameReady(d, scene.frame, ANARI_WAIT) == 1);
  uint32_t width = 0, height = 0;
  ANARIDataType pixelType = ANARI_UNKNOWN;
  auto *pixels = static_cast<const uint8_t *>(anariMapFrame(
      d, scene.frame, "channel.color", &width, &height, &pixelType));
  CHECK(pixels && width == 640 && height == 480);
  const size_t pixelBytes = size_t(width) * height * 4;
  const std::vector<uint8_t> before(pixels, pixels + pixelBytes);
  CHECK(countRegions(self) == 3);
  CHECK(seen.fatalCount() == 0);

  // The host crashes with the wrapped device's depth channel mapped and
  // nothing sent back.
  width = height = 77;
  pixelType = ANARI_FLOAT32;
  CHECK(anariMapFrame(
            d, scene.frame, "channel.depth", &width, &height, &pixelType)
      == nullptr);
  CHECK(width == 0 && height == 0 && pixelType == ANARI_UNKNOWN);

  checkDeathMessage(seen, d, "killed by signal 11");
  CHECK(!processExists(host));

  // What was mapped is still there, to read and to write.
  CHECK(countRegions(self) == 3);
  CHECK(std::memcmp(pixels, before.data(), pixelBytes) == 0);
  for (uint64_t i = 0; i < count; i++) {
    CHECK(managedMemory[i] == float(i));
    managedMemory[i] = -1.f;
  }
  std::memset(parameterMemory, 0, (count / 2) * 12);

  checkDeadDevice(scene);
  CHECK(std::memcmp(pixels, before.data(), pixelBytes) == 0);

  // Letting go is local, and gives every region back.
  anariUnmapFrame(d, scene.frame, "channel.depth"); // never mapped: nothing
  anariUnmapFrame(d, scene.frame, "channel.color");
  anariUnmapParameterArray(d, geometry, "vertex.position");
  anariUnmapArray(d, managed);
  anariRelease(d, managed);
  anariRelease(d, geometry);
  scene.release();
  CHECK(countRegions(self) == 0);

  anariRelease(d, d);
  CHECK(seen.fatal == 1);
  CHECK(seen.afterDeath == 0);
}

// The same with data below the array ceiling and the frame threshold, which
// the adapter holds in memory of its own.
static void inline_(ANARILibrary library, Seen &seen)
{
  seen.reset();
  setFault("mapFrame:2:before:segv");
  ANARIDevice d = anariNewDevice(library, "default");
  CHECK(d);
  const pid_t host = testHost();
  CHECK(host > 0);

  const uint64_t count = 256;
  ANARIArray1D managed =
      anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_FLOAT32, count);
  auto *managedMemory = static_cast<float *>(anariMapArray(d, managed));
  CHECK(managedMemory);
  for (uint64_t i = 0; i < count; i++)
    managedMemory[i] = float(i);

  Scene scene(d, 32, 24);
  anariRenderFrame(d, scene.frame);
  CHECK(anariFrameReady(d, scene.frame, ANARI_WAIT) == 1);
  uint32_t width = 0, height = 0;
  ANARIDataType pixelType = ANARI_UNKNOWN;
  auto *pixels = static_cast<const uint8_t *>(anariMapFrame(
      d, scene.frame, "channel.color", &width, &height, &pixelType));
  CHECK(pixels && width == 32 && height == 24);
  const size_t pixelBytes = size_t(width) * height * 4;
  const std::vector<uint8_t> before(pixels, pixels + pixelBytes);
  CHECK(countRegions(getpid()) == 0);

  CHECK(anariMapFrame(
            d, scene.frame, "channel.depth", &width, &height, &pixelType)
      == nullptr);
  checkDeathMessage(seen, d, "killed by signal 11");
  CHECK(!processExists(host));

  CHECK(std::memcmp(pixels, before.data(), pixelBytes) == 0);
  for (uint64_t i = 0; i < count; i++) {
    CHECK(managedMemory[i] == float(i));
    managedMemory[i] = -1.f;
  }
  checkDeadDevice(scene);
  CHECK(std::memcmp(pixels, before.data(), pixelBytes) == 0);

  anariUnmapFrame(d, scene.frame, "channel.color");
  anariUnmapArray(d, managed);
  anariRelease(d, managed);
  scene.release();
  anariRelease(d, d);
  CHECK(seen.fatal == 1);
  CHECK(seen.afterDeath == 0);
}

int main()
{
  Seen seen;
  ANARILibrary library = anariLoadLibrary("guard", Seen::callback, &seen);
  CHECK(library);

  inRegions(library, seen);
  inline_(library, seen);

  anariUnloadLibrary(library);
  std::printf("passed\n");
  return 0;
}
