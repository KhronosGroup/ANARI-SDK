// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// What the wrapped library and device say while the wrapped device is being
// created reaches the application as coming from the library, with no device:
// until anariNewDevice returns there is no guard device to name, and when
// creation fails there never is one.

#include "Recording.h"

using namespace guard_test;

static void checkFromLibrary(const StatusLog::Message &m)
{
  CHECK(m.device == nullptr);
  CHECK(m.source == nullptr);
  CHECK(m.sourceType == ANARI_LIBRARY);
}

int main()
{
  const std::string log = useRecordingDevice();

  StatusLog status;
  ANARILibrary library =
      anariLoadLibrary("guard", StatusLog::callback, &status);
  CHECK(library);

  // Creation failure: everything is delivered before anariNewDevice returns,
  // and none of it carries a device.
  CHECK(anariNewDevice(library, "broken@guard_recorder") == nullptr);
  const size_t afterFailure = status.count();
  CHECK(status.matching("recorder: about to fail").size() == 1);
  const auto reason = status.matching("has no device 'broken'");
  CHECK(reason.size() == 1);
  CHECK(reason[0].severity == ANARI_SEVERITY_ERROR);
  {
    std::lock_guard<std::mutex> lock(status.mutex);
    for (auto &m : status.messages)
      checkFromLibrary(m);
  }
  CHECK(findChild(getpid(), "anariGuardHost") == -1);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(status.count() == afterFailure);

  // Creation that succeeds: what was said before the device existed still
  // comes from the library; what is said afterwards comes from the device.
  ANARIDevice d = anariNewDevice(library, "default");
  CHECK(d);
  anariSetParameter(d, d, "emitStatus", ANARI_STRING, "from the device");
  int32_t answer = 0;
  CHECK(anariGetProperty(d, d, "answer", ANARI_INT32, &answer, 4, ANARI_WAIT));
  CHECK(
      waitFor([&] { return status.matching("from the device").size() == 1; }));

  const auto loading = status.matching("recorder: loading");
  CHECK(loading.size() == 1);
  checkFromLibrary(loading[0]);
  const auto constructed = status.matching("recorder: device constructed");
  CHECK(constructed.size() == 1);
  checkFromLibrary(constructed[0]);
  const auto later = status.matching("from the device");
  CHECK(later[0].device == d);
  CHECK(later[0].source == (ANARIObject)d);
  CHECK(later[0].sourceType == ANARI_DEVICE);

  anariRelease(d, d);
  anariUnloadLibrary(library);
  unlink(log.c_str());
  std::printf("passed\n");
  return 0;
}
