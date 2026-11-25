// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// An element of an object array that the application leaves alone keeps
// naming its object after the application has released the handle: the
// wrapped device's array still holds it at the next unmap, the application
// is told of no error, and the host is never sent an id it has forgotten
// (which would end it).

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

  // A shared array: the elements are the application's own memory.
  ANARISurface first = anariNewSurface(d);
  expect("newSurface -> #1");
  ANARISurface second = anariNewSurface(d);
  expect("newSurface -> #2");
  ANARISurface shared[2] = {first, second};
  ANARIArray1D array =
      anariNewArray1D(d, shared, nullptr, nullptr, ANARI_SURFACE, 2);
  expect("newArray1D ANARI_SURFACE 2 [#1 #2] -> #3");
  anariRelease(d, first);
  expect("release #1");

  // Nothing changed.
  CHECK(anariMapArray(d, array) == shared);
  anariUnmapArray(d, array);
  expect("unmapArray #3 [#1 #2]");

  // The other element rewritten.
  ANARISurface third = anariNewSurface(d);
  expect("newSurface -> #4");
  CHECK(anariMapArray(d, array) == shared);
  shared[1] = third;
  anariUnmapArray(d, array);
  expect("unmapArray #3 [#1 #4]");

  // A managed array, read back and one element left alone.
  ANARIArray1D managed =
      anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_SURFACE, 2);
  expect("newArray1D ANARI_SURFACE 2 [null null] -> #5");
  auto *mem = static_cast<ANARISurface *>(anariMapArray(d, managed));
  CHECK(mem);
  mem[0] = second;
  mem[1] = third;
  anariUnmapArray(d, managed);
  expect("unmapArray #5 [#2 #4]");
  anariRelease(d, second);
  expect("release #2");
  ANARISurface fourth = anariNewSurface(d);
  expect("newSurface -> #6");
  mem = static_cast<ANARISurface *>(anariMapArray(d, managed));
  CHECK(mem && mem[0] == second && mem[1] == third);
  mem[1] = fourth;
  anariUnmapArray(d, managed);
  expect("unmapArray #5 [#2 #6]");

  // An array of arrays.
  const float values[2] = {1.f, 2.f};
  ANARIArray1D innerA =
      anariNewArray1D(d, &values[0], nullptr, nullptr, ANARI_FLOAT32, 1);
  expect("newArray1D ANARI_FLOAT32 1 [0000803f] -> #7");
  ANARIArray1D innerB =
      anariNewArray1D(d, &values[1], nullptr, nullptr, ANARI_FLOAT32, 1);
  expect("newArray1D ANARI_FLOAT32 1 [00000040] -> #8");
  ANARIArray1D inner[2] = {innerA, innerB};
  ANARIArray1D outer =
      anariNewArray1D(d, inner, nullptr, nullptr, ANARI_ARRAY1D, 2);
  expect("newArray1D ANARI_ARRAY1D 2 [#7 #8] -> #9");
  anariRelease(d, innerA);
  expect("release #7");
  anariMapArray(d, outer);
  anariUnmapArray(d, outer);
  expect("unmapArray #9 [#7 #8]");

  // A larger array: 2500 handles are 20 KB, which cross in a region when the
  // test is run with an array ceiling below that.
  const size_t many = 2500;
  std::vector<ANARISurface> surfaces(many);
  std::string names;
  for (size_t i = 0; i < many; i++) {
    surfaces[i] = anariNewSurface(d);
    const std::string name = "#" + std::to_string(10 + i);
    expect("newSurface -> " + name);
    names += (i ? " " : "") + name;
  }
  ANARIArray1D large = anariNewArray1D(
      d, surfaces.data(), nullptr, nullptr, ANARI_SURFACE, many);
  const std::string largeName = "#" + std::to_string(10 + many);
  expect("newArray1D ANARI_SURFACE 2500 [" + names + "] -> " + largeName);
  anariRelease(d, surfaces[0]);
  expect("release #10");
  anariRelease(d, surfaces[many - 1]);
  expect("release #" + std::to_string(10 + many - 1));
  anariMapArray(d, large);
  surfaces[1] = third;
  anariUnmapArray(d, large);
  names.replace(names.find("#11"), 3, "#4");
  expect("unmapArray " + largeName + " [" + names + "]");

  // The device is alive, and the application was told of nothing.
  int32_t answer = 0;
  CHECK(anariGetProperty(d, d, "answer", ANARI_INT32, &answer, 4, ANARI_WAIT));
  CHECK(answer == 42);
  expect("getProperty device \"answer\" ANARI_INT32 size=4 wait=1 -> 1");
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  {
    std::lock_guard<std::mutex> lock(status.mutex);
    for (auto &m : status.messages) {
      if (m.text.rfind("recorder:", 0) != 0) {
        std::fprintf(stderr, "FAILED: told \"%s\"\n", m.text.c_str());
        return 1;
      }
    }
  }

  anariRelease(d, d);
  expect("release device");
  anariUnloadLibrary(library);

  checkLog(log, expected);
  std::printf("passed\n");
  return 0;
}
