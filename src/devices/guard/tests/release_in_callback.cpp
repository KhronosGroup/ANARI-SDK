// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// An application may release the device for the last time from inside one of
// its own callbacks. The release returns at once, the guard device finishes
// taking itself down after the callback has returned, and unloading the
// library waits for that.

#include "Recording.h"

#include <atomic>

using namespace guard_test;

static std::atomic<int> released{0};

static void releaseFromCompletion(
    const void *, ANARIDevice device, ANARIFrame frame)
{
  anariRelease(device, frame);
  anariRelease(device, device);
  released++;
}

static void releaseFromFatalStatus(const void *,
    ANARIDevice device,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *)
{
  if (severity == ANARI_SEVERITY_FATAL_ERROR) {
    anariRelease(device, device);
    released++;
  }
}

// With the host alive: from a frame's completion callback.
static void fromCompletionCallback()
{
  const std::string log = useRecordingDevice();
  ANARILibrary library = anariLoadLibrary("guard", nullptr, nullptr);
  CHECK(library);
  ANARIDevice d = anariNewDevice(library, "default");
  CHECK(d);
  const pid_t host = findChild(getpid(), "anariGuardHost");
  CHECK(host > 0);

  ANARIFrame frame = anariNewFrame(d);
  ANARIFrameCompletionCallback callback = releaseFromCompletion;
  anariSetParameter(d,
      frame,
      "frameCompletionCallback",
      ANARI_FRAME_COMPLETION_CALLBACK,
      &callback);
  anariCommitParameters(d, frame);
  released = 0;
  anariRenderFrame(d, frame);

  // From here the device is the callback's to release: no more calls on it.
  CHECK(waitFor([&] { return released == 1; }));
  anariUnloadLibrary(library);

  // The wrapped device was released in an orderly way and the host is gone.
  CHECK(!processExists(host));
  checkLog(log,
      {"newFrame -> #1",
          "commitParameters #1",
          "renderFrame #1",
          "release #1",
          "release device"});
}

// With the host gone: from the status callback that tells of device death,
// which runs under the call that delivers the message. That call finishes
// the release before it returns.
static void fromFatalStatusCallback()
{
  const std::string log = useRecordingDevice();
  ANARILibrary library =
      anariLoadLibrary("guard", releaseFromFatalStatus, nullptr);
  CHECK(library);
  ANARIDevice d = anariNewDevice(library, "default");
  CHECK(d);
  int32_t answer = 0;
  CHECK(anariGetProperty(d, d, "answer", ANARI_INT32, &answer, 4, ANARI_WAIT));

  const pid_t host = findChild(getpid(), "anariGuardHost");
  CHECK(host > 0);
  released = 0;
  CHECK(kill(host, SIGKILL) == 0);
  CHECK(waitFor([&] { return !processExists(host); }));
  CHECK(released == 0);

  CHECK(!anariGetProperty(d, d, "answer", ANARI_INT32, &answer, 4, ANARI_WAIT));
  CHECK(released == 1);
  anariUnloadLibrary(library);
  unlink(log.c_str());
}

int main()
{
  fromCompletionCallback();
  fromFatalStatusCallback();
  std::printf("passed\n");
  return 0;
}
