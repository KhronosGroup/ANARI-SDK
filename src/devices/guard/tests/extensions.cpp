// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// What cannot cross a process boundary is not offered and not passed on:
// extensions that hand out or take device memory or native handles are
// missing from every extension list, and the frame channels and parameters
// that go with them are refused in the adapter.

#include "Recording.h"

using namespace guard_test;

static void checkList(const char *const *list)
{
  CHECK(list);
  const char *kept[] = {"ANARI_KHR_GEOMETRY_TRIANGLE",
      "ANARI_KHR_FRAME_COMPLETION_CALLBACK",
      "ANARI_KHR_CAMERA_PERSPECTIVE",
      // What several threads may do is the wrapped device's to offer, and the
      // guard device keeps the offer.
      "ANARI_KHR_DEVICE_SYNCHRONIZATION"};
  for (int i = 0; i < 4; i++)
    CHECK(list[i] && std::string(list[i]) == kept[i]);
  CHECK(list[4] == nullptr);
}

int main()
{
  const std::string log = useRecordingDevice();
  std::vector<std::string> expected;

  StatusLog status;
  ANARILibrary library =
      anariLoadLibrary("guard", StatusLog::callback, &status);
  CHECK(library);

  // Before any device exists.
  checkList(anariGetDeviceExtensions(library, "default"));

  ANARIDevice d = anariNewDevice(library, "default");
  CHECK(d);

  // The two ways a device is asked.
  const char *const *list = nullptr;
  CHECK(anariGetProperty(
      d, d, "extension", ANARI_STRING_LIST, &list, sizeof(list), ANARI_WAIT));
  expected.push_back(
      "getProperty device \"extension\" ANARI_STRING_LIST size=8 wait=1 -> 1");
  checkList(list);
  checkList(static_cast<const char *const *>(anariGetObjectInfo(
      d, ANARI_DEVICE, "default", "extension", ANARI_STRING_LIST)));
  expected.push_back(
      "getObjectInfo ANARI_DEVICE \"default\" \"extension\" ANARI_STRING_LIST");

  ANARIFrame frame = anariNewFrame(d);
  expected.push_back("newFrame -> #1");
  const uint32_t size[2] = {4, 3};
  anariSetParameter(d, frame, "size", ANARI_UINT32_VEC2, size);
  expected.push_back(
      "setParameter #1 \"size\" ANARI_UINT32_VEC2 [0400000003000000]");

  // A channel of device memory: the host could not read it, so it is never
  // asked to.
  uint32_t width = 77, height = 77;
  ANARIDataType pixelType = ANARI_FLOAT32;
  CHECK(
      anariMapFrame(d, frame, "channel.colorCUDA", &width, &height, &pixelType)
      == nullptr);
  CHECK(width == 0 && height == 0 && pixelType == ANARI_UNKNOWN);
  anariUnmapFrame(d, frame, "channel.colorCUDA");

  // A parameter that is a pointer into this process.
  int communicator = 0;
  anariSetParameter(
      d, frame, "mpiCommunicator", ANARI_VOID_POINTER, &communicator);

  // The device is none the worse, and was told about neither.
  int32_t answer = 0;
  CHECK(anariGetProperty(d, d, "answer", ANARI_INT32, &answer, 4, ANARI_WAIT));
  expected.push_back(
      "getProperty device \"answer\" ANARI_INT32 size=4 wait=1 -> 1");
  CHECK(answer == 42);

  CHECK(
      waitFor([&] { return status.matching("mpiCommunicator").size() == 1; }));
  for (const char *what : {"channel.colorCUDA", "mpiCommunicator"}) {
    const auto told = status.matching(what);
    CHECK(told.size() == 1);
    CHECK(told[0].device == d);
    CHECK(told[0].source == (ANARIObject)frame);
    CHECK(told[0].severity == ANARI_SEVERITY_ERROR);
    CHECK(told[0].code == ANARI_STATUS_INVALID_ARGUMENT);
  }

  anariRelease(d, frame);
  expected.push_back("release #1");
  anariRelease(d, d);
  expected.push_back("release device");
  anariUnloadLibrary(library);

  checkLog(log, expected);
  std::printf("passed\n");
  return 0;
}
