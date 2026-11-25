// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// For the tests of device death at a chosen call: the fault asked of
// guardTestHost (TestHost.cpp), what the application is told, a small scene
// to render, and the signature of a dead device.

#include "TestSupport.h"

#include <anari/anari.h>

#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace guard_test {

static const char *const TEST_HOST = "guardTestHost";

// The next host started dies at '<call>:<nth>:<when>:<how>'.
inline void setFault(const char *fault)
{
  // The tests are run with ANARI_GUARD_HOST naming the test host; with the
  // plain host no fault would ever come.
  const char *host = std::getenv("ANARI_GUARD_HOST");
  CHECK(host && std::string(host).find(TEST_HOST) != std::string::npos);
  setenv("ANARI_GUARD_HOST_FAULT", fault, 1);
}

inline void clearFault()
{
  unsetenv("ANARI_GUARD_HOST_FAULT");
}

inline pid_t testHost()
{
  return findChild(getpid(), TEST_HOST);
}

// What the status callback was given.
struct Seen
{
  std::mutex mutex;
  int fatal{0};
  ANARIDevice device{nullptr};
  ANARIObject source{nullptr};
  ANARIDataType sourceType{ANARI_UNKNOWN};
  ANARIStatusCode code{ANARI_STATUS_NO_ERROR};
  std::string message;
  std::thread::id thread; // the one the fatal message was delivered on
  int afterDeath{0}; // anything reported once the fatal message has arrived

  // For the next device of the same test.
  void reset()
  {
    std::lock_guard<std::mutex> lock(mutex);
    fatal = 0;
    afterDeath = 0;
    message.clear();
  }

  int fatalCount()
  {
    std::lock_guard<std::mutex> lock(mutex);
    return fatal;
  }

  static void callback(const void *userData,
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
      seen.thread = std::this_thread::get_id();
    } else if (seen.fatal > 0 && severity <= ANARI_SEVERITY_WARNING)
      seen.afterDeath++;
  }
};

// The library part of a wrapped device identifier, '[subtype@]library'.
inline std::string libraryOf(const std::string &identifier)
{
  const auto at = identifier.find('@');
  return at == std::string::npos ? identifier : identifier.substr(at + 1);
}

// The host is gone and the adapter knows it, and the application, which has
// made no call since, has not been told: the death message waits for a call.
// Returns false in the one case where that cannot be seen: the host died so
// soon that the call that made it die had not yet returned, and so was itself
// the call that told.
inline bool checkNotTold(Seen &seen, pid_t host)
{
  if (seen.fatalCount() > 0)
    return false;
  CHECK(waitFor([&] { return !processExists(host); }));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(seen.fatalCount() == 0);
  return true;
}

// The application has been told of the death of 'device' exactly once, by
// the ANARI call that has just returned on 'thread' (this one, unless it
// says otherwise): a fatal message from the device that names the wrapped
// library and says how the host ended ('how' is part of that, such as
// "killed by signal 11"). Nothing is waited for.
inline void checkDeathMessage(Seen &seen,
    ANARIDevice device,
    const std::string &how,
    const char *wrapped = nullptr,
    std::thread::id thread = std::this_thread::get_id())
{
  std::lock_guard<std::mutex> lock(seen.mutex);
  if (seen.fatal != 1) {
    std::fprintf(stderr,
        "FAILED: the application was told of the death %d times, not once\n",
        seen.fatal);
    std::exit(1);
  }
  CHECK(seen.thread == thread);
  CHECK(seen.device == device);
  CHECK(seen.source == (ANARIObject)device);
  CHECK(seen.sourceType == ANARI_DEVICE);
  CHECK(seen.code == ANARI_STATUS_UNKNOWN_ERROR);
  if (!wrapped)
    wrapped = std::getenv("ANARI_GUARD_WRAPPED_DEVICE");
  CHECK(wrapped);
  if (seen.message.find("'" + libraryOf(wrapped) + "'") == std::string::npos
      || seen.message.find(how) == std::string::npos) {
    std::fprintf(stderr,
        "FAILED: the fatal message does not name '%s' and say '%s':\n  %s\n",
        libraryOf(wrapped).c_str(),
        how.c_str(),
        seen.message.c_str());
    std::exit(1);
  }
}

// A frame that can be rendered, and everything it needs.
struct Scene
{
  ANARIDevice device{nullptr};
  ANARICamera camera{nullptr};
  ANARIRenderer renderer{nullptr};
  ANARIWorld world{nullptr};
  ANARIFrame frame{nullptr};
  uint32_t size[2]{64, 48};

  Scene(ANARIDevice d, uint32_t width = 64, uint32_t height = 48) : device(d)
  {
    size[0] = width;
    size[1] = height;
    const ANARIDataType colorType = ANARI_UFIXED8_RGBA_SRGB;
    const ANARIDataType depthType = ANARI_FLOAT32;
    camera = anariNewCamera(d, "perspective");
    anariCommitParameters(d, camera);
    renderer = anariNewRenderer(d, "default");
    anariCommitParameters(d, renderer);
    world = anariNewWorld(d);
    anariCommitParameters(d, world);
    frame = anariNewFrame(d);
    anariSetParameter(d, frame, "size", ANARI_UINT32_VEC2, size);
    anariSetParameter(d, frame, "channel.color", ANARI_DATA_TYPE, &colorType);
    anariSetParameter(d, frame, "channel.depth", ANARI_DATA_TYPE, &depthType);
    anariSetParameter(d, frame, "camera", ANARI_CAMERA, &camera);
    anariSetParameter(d, frame, "renderer", ANARI_RENDERER, &renderer);
    anariSetParameter(d, frame, "world", ANARI_WORLD, &world);
    anariCommitParameters(d, frame);
    CHECK(camera && renderer && world && frame);
  }

  // Makes the next render one the wrapped device cannot decline.
  void change()
  {
    m_aspect += 0.01f;
    anariSetParameter(device, camera, "aspect", ANARI_FLOAT32, &m_aspect);
    anariCommitParameters(device, camera);
  }

  void release()
  {
    anariRelease(device, frame);
    anariRelease(device, world);
    anariRelease(device, renderer);
    anariRelease(device, camera);
  }

 private:
  float m_aspect{1.f};
};

// The calls of a dead device do nothing and return what was decided: no
// property, no mapped frame, a frame that is always ready, handles that are
// still handles.
inline void checkDeadDevice(Scene &scene)
{
  ANARIDevice d = scene.device;

  ANARIGeometry geometry = anariNewGeometry(d, "triangle");
  CHECK(geometry);
  CHECK((ANARIObject)geometry != (ANARIObject)scene.camera);
  anariCommitParameters(d, geometry);
  anariRelease(d, geometry);

  int32_t property = 1234;
  CHECK(anariGetProperty(d,
            scene.frame,
            "numSamples",
            ANARI_INT32,
            &property,
            sizeof(property),
            ANARI_WAIT)
      == 0);
  CHECK(property == 1234);

  uint32_t width = 77, height = 77;
  ANARIDataType pixelType = ANARI_FLOAT32;
  CHECK(anariMapFrame(
            d, scene.frame, "channel.color", &width, &height, &pixelType)
      == nullptr);
  CHECK(width == 0 && height == 0 && pixelType == ANARI_UNKNOWN);

  anariRenderFrame(d, scene.frame);
  CHECK(anariFrameReady(d, scene.frame, ANARI_NO_WAIT) == 1);
  CHECK(anariFrameReady(d, scene.frame, ANARI_WAIT) == 1);
  anariDiscardFrame(d, scene.frame);

  CHECK(anariGetObjectSubtypes(d, ANARI_VOLUME) == nullptr);
}

} // namespace guard_test
