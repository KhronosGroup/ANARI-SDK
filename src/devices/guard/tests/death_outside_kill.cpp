// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Device death with the host idle, induced from outside with SIGKILL and the
// plain anariGuardHost: the application is told once, by its next call, and
// carries on calling ANARI on the dead device.

#include "TestSupport.h"

#include <anari/anari.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

#include <unistd.h>

using namespace guard_test;

struct Seen
{
  std::mutex mutex;
  int fatal{0};
  ANARIDevice device{nullptr};
  ANARIObject source{nullptr};
  ANARIDataType sourceType{ANARI_UNKNOWN};
  ANARIStatusCode code{ANARI_STATUS_NO_ERROR};
  std::string message;
  int afterDeath{0}; // anything reported once the fatal message has arrived
};

static void onStatus(const void *userData,
    ANARIDevice device,
    ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message)
{
  auto &seen = *static_cast<Seen *>(const_cast<void *>(userData));
  std::lock_guard<std::mutex> lock(seen.mutex);
  if (severity == ANARI_SEVERITY_FATAL_ERROR) {
    seen.fatal++;
    seen.device = device;
    seen.source = source;
    seen.sourceType = sourceType;
    seen.code = code;
    seen.message = message;
  } else if (seen.fatal > 0 && severity <= ANARI_SEVERITY_WARNING)
    seen.afterDeath++;
}

static std::atomic<int> deleterRuns{0};

static void onDelete(const void *, const void *)
{
  deleterRuns++;
}

int main()
{
  Seen seen;
  ANARILibrary library = anariLoadLibrary("guard", onStatus, &seen);
  CHECK(library);
  ANARIDevice device = anariNewDevice(library, "default");
  CHECK(device);

  // A frame rendered and mapped, and an answered introspection query, all
  // from before the death.
  const uint32_t size[2] = {64, 48};
  const ANARIDataType colorType = ANARI_UFIXED8_RGBA_SRGB;
  ANARICamera camera = anariNewCamera(device, "perspective");
  anariCommitParameters(device, camera);
  ANARIRenderer renderer = anariNewRenderer(device, "default");
  anariCommitParameters(device, renderer);
  ANARIWorld world = anariNewWorld(device);
  anariCommitParameters(device, world);
  ANARIFrame frame = anariNewFrame(device);
  anariSetParameter(device, frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(
      device, frame, "channel.color", ANARI_DATA_TYPE, &colorType);
  anariSetParameter(device, frame, "camera", ANARI_CAMERA, &camera);
  anariSetParameter(device, frame, "renderer", ANARI_RENDERER, &renderer);
  anariSetParameter(device, frame, "world", ANARI_WORLD, &world);
  anariCommitParameters(device, frame);
  anariRenderFrame(device, frame);
  CHECK(anariFrameReady(device, frame, ANARI_WAIT) == 1);

  uint32_t width = 0, height = 0;
  ANARIDataType pixelType = ANARI_UNKNOWN;
  auto *pixels = static_cast<const uint8_t *>(anariMapFrame(
      device, frame, "channel.color", &width, &height, &pixelType));
  CHECK(pixels && width == size[0] && height == size[1]);
  const std::vector<uint8_t> before(pixels, pixels + width * height * 4);

  const char **subtypes = anariGetObjectSubtypes(device, ANARI_RENDERER);
  CHECK(subtypes && subtypes[0]);

  std::vector<float> shared(300, 1.f);
  ANARIArray1D array = anariNewArray1D(
      device, shared.data(), onDelete, nullptr, ANARI_FLOAT32_VEC3, 100);
  CHECK(array);

  // Device death.
  const pid_t host = findChild(getpid(), "anariGuardHost");
  CHECK(host > 0);
  CHECK(kill(host, SIGKILL) == 0);

  // The adapter reaps the host when it has seen the death. The application
  // hears of it from the first call it then makes.
  CHECK(waitFor([&] { return !processExists(host); }));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  {
    std::lock_guard<std::mutex> lock(seen.mutex);
    CHECK(seen.fatal == 0);
  }
  ANARIGeometry geometry = anariNewGeometry(device, "triangle");
  {
    std::lock_guard<std::mutex> lock(seen.mutex);
    CHECK(seen.fatal == 1);
    CHECK(seen.device == device);
    CHECK(seen.source == (ANARIObject)device);
    CHECK(seen.sourceType == ANARI_DEVICE);
    CHECK(seen.code == ANARI_STATUS_UNKNOWN_ERROR);
    const char *wrapped = std::getenv("ANARI_GUARD_WRAPPED_DEVICE");
    CHECK(wrapped && seen.message.find(wrapped) != std::string::npos);
    CHECK(seen.message.find("killed by signal 9") != std::string::npos);
  }

  // The dead device: handles stay valid, calls do nothing.
  CHECK(geometry);
  CHECK((ANARIObject)geometry != (ANARIObject)camera);
  anariSetParameter(
      device, geometry, "primitive.position", ANARI_ARRAY1D, &array);
  anariCommitParameters(device, geometry);
  anariUnsetParameter(device, geometry, "primitive.position");

  // What was mapped before the death is still there.
  CHECK(std::memcmp(pixels, before.data(), before.size()) == 0);
  anariUnmapFrame(device, frame, "channel.color");

  int32_t property = 1234;
  CHECK(anariGetProperty(
            device, frame, "numSamples", ANARI_INT32, &property, 4, ANARI_WAIT)
      == 0);
  CHECK(property == 1234);

  width = height = 77;
  pixelType = ANARI_FLOAT32;
  CHECK(
      anariMapFrame(device, frame, "channel.color", &width, &height, &pixelType)
      == nullptr);
  CHECK(width == 0 && height == 0 && pixelType == ANARI_UNKNOWN);

  anariRenderFrame(device, frame);
  CHECK(anariFrameReady(device, frame, ANARI_NO_WAIT) == 1);
  CHECK(anariFrameReady(device, frame, ANARI_WAIT) == 1);
  anariDiscardFrame(device, frame);

  // An introspection query answered before the death is answered again; a new
  // one is not.
  CHECK(anariGetObjectSubtypes(device, ANARI_RENDERER) == subtypes);
  CHECK(anariGetObjectSubtypes(device, ANARI_MATERIAL) == nullptr);

  // Arrays stay local: mapping works and the deleter runs at the last release.
  CHECK(anariMapArray(device, array) == shared.data());
  anariUnmapArray(device, array);
  anariRetain(device, array);
  anariRelease(device, array);
  CHECK(deleterRuns == 0);
  anariRelease(device, array);
  CHECK(deleterRuns == 1);

  anariRelease(device, geometry);
  anariRelease(device, frame);
  anariRelease(device, world);
  anariRelease(device, renderer);
  anariRelease(device, camera);
  anariRelease(device, device);
  anariUnloadLibrary(library);

  CHECK(seen.fatal == 1);
  CHECK(seen.afterDeath == 0);
  CHECK(!processExists(host));
  std::printf("passed\n");
  return 0;
}
