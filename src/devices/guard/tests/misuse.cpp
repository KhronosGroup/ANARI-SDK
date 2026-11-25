// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// An application that passes a handle the guard device never gave it, or one
// it has already released for the last time, is told so. The call does
// nothing, the host never hears of it, and the device lives on.

#include "Recording.h"

using namespace guard_test;

int main()
{
  const std::string log = useRecordingDevice();
  std::vector<std::string> expected;
  auto expect = [&](const std::string &line) { expected.push_back(line); };

  StatusLog status;
  ANARILibrary library =
      anariLoadLibrary("guard", StatusLog::callback, &status);
  CHECK(library);
  ANARIDevice d = anariNewDevice(library, "default");
  CHECK(d);

  ANARISurface surface = anariNewSurface(d);
  expect("newSurface -> #1");
  ANARIGeometry geometry = anariNewGeometry(d, "triangle");
  expect("newGeometry \"triangle\" -> #2");
  ANARIFrame frame = anariNewFrame(d);
  expect("newFrame -> #3");

  // Never a handle of this device.
  const ANARIObject stranger = ANARIObject(uintptr_t(0x7f0000001000));
  // A handle once, until its last release.
  ANARIGeometry stale = anariNewGeometry(d, "triangle");
  expect("newGeometry \"triangle\" -> #4");
  anariRetain(d, stale);
  expect("retain #4");
  anariRelease(d, stale);
  expect("release #4");
  anariRelease(d, stale);
  expect("release #4");

  size_t refusals = 0;
  for (ANARIObject bad : {stranger, ANARIObject(stale)}) {
    const float value = 1.f;
    uint64_t stride = 0;
    int32_t property = 1234;
    uint32_t width = 77, height = 77;
    ANARIDataType pixelType = ANARI_FLOAT32;

    anariSetParameter(d, bad, "radius", ANARI_FLOAT32, &value);
    anariUnsetParameter(d, bad, "radius");
    anariUnsetAllParameters(d, bad);
    anariCommitParameters(d, bad);
    CHECK(anariMapParameterArray1D(
              d, bad, "vertex.position", ANARI_FLOAT32, 4, &stride)
        == nullptr);
    anariUnmapParameterArray(d, bad, "vertex.position");
    anariRetain(d, bad);
    anariRelease(d, bad);
    CHECK(anariMapArray(d, ANARIArray(bad)) == nullptr);
    anariUnmapArray(d, ANARIArray(bad));
    CHECK(anariGetProperty(
              d, bad, "answer", ANARI_INT32, &property, 4, ANARI_WAIT)
        == 0);
    CHECK(property == 1234);
    anariRenderFrame(d, ANARIFrame(bad));
    CHECK(anariFrameReady(d, ANARIFrame(bad), ANARI_WAIT) == 1);
    anariDiscardFrame(d, ANARIFrame(bad));
    CHECK(anariMapFrame(
              d, ANARIFrame(bad), "channel.color", &width, &height, &pixelType)
        == nullptr);
    CHECK(width == 0 && height == 0 && pixelType == ANARI_UNKNOWN);
    anariUnmapFrame(d, ANARIFrame(bad), "channel.color");
    refusals += 15; // every call above but the unmap of a frame never mapped

    // As a parameter's value: the parameter is not set.
    anariSetParameter(d, surface, "geometry", ANARI_GEOMETRY, &bad);
    refusals++;

    // As an element of an array: the array is made, with nothing there.
    ANARIObject elements[3] = {surface, bad, geometry};
    ANARIArray1D array =
        anariNewArray1D(d, elements, nullptr, nullptr, ANARI_OBJECT, 3);
    CHECK(array);
    expect(std::string("newArray1D ANARI_OBJECT 3 [#1 null #2] -> #")
        + (bad == stranger ? "5" : "6"));
    refusals++;
  }

  // An object that is not a frame, where a frame is asked for.
  anariRenderFrame(d, ANARIFrame(geometry));
  CHECK(anariFrameReady(d, ANARIFrame(geometry), ANARI_WAIT) == 1);
  uint32_t width = 77, height = 77;
  ANARIDataType pixelType = ANARI_FLOAT32;
  CHECK(
      anariMapFrame(
          d, ANARIFrame(geometry), "channel.color", &width, &height, &pixelType)
      == nullptr);
  refusals += 3;
  // And one that is not an array.
  CHECK(anariMapArray(d, ANARIArray(geometry)) == nullptr);
  anariUnmapArray(d, ANARIArray(geometry));
  refusals += 2;

  // The device still works, and its real handles are untouched.
  anariCommitParameters(d, geometry);
  expect("commitParameters #2");
  anariRenderFrame(d, frame);
  expect("renderFrame #3");
  CHECK(anariFrameReady(d, frame, ANARI_WAIT) == 1);
  int32_t answer = 0;
  CHECK(anariGetProperty(d, d, "answer", ANARI_INT32, &answer, 4, ANARI_WAIT));
  expect("getProperty device \"answer\" ANARI_INT32 size=4 wait=1 -> 1");

  // One error for each refused call, from the device, and nothing else.
  CHECK(waitFor([&] { return status.count() >= refusals; }));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  size_t errors = 0;
  {
    std::lock_guard<std::mutex> lock(status.mutex);
    for (auto &m : status.messages) {
      if (m.text.rfind("recorder:", 0) == 0)
        continue; // what the recording device says as it starts
      CHECK(m.device == d);
      CHECK(m.source == (ANARIObject)d);
      CHECK(m.sourceType == ANARI_DEVICE);
      CHECK(m.severity == ANARI_SEVERITY_ERROR);
      CHECK(m.code == ANARI_STATUS_INVALID_ARGUMENT);
      errors++;
    }
  }
  CHECK(errors == refusals);
  CHECK(status.matching("anariCommitParameters").size() == 2);
  CHECK(status.matching("anariNewArray1D").size() == 2);

  anariRelease(d, d);
  expect("release device");
  anariUnloadLibrary(library);

  checkLog(log, expected);
  std::printf("passed\n");
  return 0;
}
