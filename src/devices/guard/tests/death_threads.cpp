// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Device death with several threads of the application calling, each on its
// own objects. Exactly one of their calls tells the application, every thread
// comes back from the call it was in, and what they call afterwards does
// nothing.

#include "DeathTest.h"

#include <algorithm>
#include <atomic>

using namespace guard_test;

static const int THREADS = 6;

// The device lists the extension that lets threads call it at once.
static void checkSynchronized(ANARIDevice d)
{
  const char *const *list = nullptr;
  CHECK(anariGetProperty(
      d, d, "extension", ANARI_STRING_LIST, &list, sizeof(list), ANARI_WAIT));
  CHECK(list);
  bool listed = false;
  for (; *list; list++)
    listed |= std::string(*list) == "ANARI_KHR_DEVICE_SYNCHRONIZATION";
  CHECK(listed);
}

// One object of the thread's own, made, filled and released: calls that are
// pipelined, a region, and one that waits for the host's answer.
static void workOnOwnObjects(ANARIDevice d, int t, int round)
{
  ANARIGeometry geometry = anariNewGeometry(d, "triangle");
  CHECK(geometry);

  uint64_t stride = 0;
  auto *positions = static_cast<float *>(anariMapParameterArray1D(
      d, geometry, "vertex.position", ANARI_FLOAT32_VEC3, 3, &stride));
  if (positions) { // a dead device maps nothing
    for (int i = 0; i < 9; i++)
      positions[i] = float(t + i + round);
    anariUnmapParameterArray(d, geometry, "vertex.position");
  }

  const std::vector<float> colors(4 * 6000, 0.5f); // in a region
  ANARIArray1D array = anariNewArray1D(
      d, colors.data(), nullptr, nullptr, ANARI_FLOAT32_VEC4, 6000);
  CHECK(array);
  anariSetParameter(d, geometry, "primitive.color", ANARI_ARRAY1D, &array);
  anariCommitParameters(d, geometry);

  int32_t value = 0;
  anariGetProperty(d,
      geometry,
      "guardTestNoSuchProperty",
      ANARI_INT32,
      &value,
      4,
      ANARI_WAIT);

  anariRelease(d, array);
  anariRelease(d, geometry);
}

// One device whose host is ended while THREADS threads are calling: by
// 'fault' if there is one, else from outside.
static void run(ANARILibrary library, Seen &seen, const char *fault)
{
  seen.reset();
  if (fault)
    setFault(fault);
  else
    clearFault();
  ANARIDevice device = anariNewDevice(library, "default");
  CHECK(device);
  const pid_t host = testHost();
  CHECK(host > 0);
  checkSynchronized(device);

  Scene scene(device);
  anariRenderFrame(device, scene.frame);
  CHECK(anariFrameReady(device, scene.frame, ANARI_WAIT) == 1);
  CHECK(seen.fatalCount() == 0);

  std::atomic<bool> go{false};
  std::atomic<int> working{0};
  std::atomic<int> done{0};
  std::vector<std::thread> threads;
  std::vector<std::thread::id> ids;
  for (int t = 0; t < THREADS; t++) {
    threads.emplace_back([&, t] {
      while (!go)
        std::this_thread::yield();
      // Until the application is told, by this thread or another. A thread
      // that was not the one to tell has by then come back from a call, and
      // maybe from many, on a device that was already dead.
      int round = 0;
      while (seen.fatalCount() == 0) {
        workOnOwnObjects(device, t, round);
        if (++round == 3)
          working++;
      }
      // And on a device every thread knows is dead.
      for (int i = 0; i < 20; i++)
        workOnOwnObjects(device, t, round++);
      done++;
    });
    ids.push_back(threads.back().get_id());
  }
  go = true;

  if (!fault) {
    CHECK(waitFor([&] { return working == THREADS; }));
    CHECK(seen.fatalCount() == 0);
    CHECK(kill(host, SIGKILL) == 0);
  }

  // Every thread returns: none is left waiting for a reply, for the
  // dispatcher, or for the thread that tells.
  if (!waitFor([&] { return done == THREADS; }, 30.0)) {
    std::fprintf(stderr,
        "FAILED: %d of %d threads did not return after device death\n",
        THREADS - done.load(),
        THREADS);
    std::exit(1);
  }
  for (auto &thread : threads)
    thread.join();

  // Told once, by a call one of the threads was making.
  std::thread::id teller;
  {
    std::lock_guard<std::mutex> lock(seen.mutex);
    CHECK(seen.fatal == 1);
    teller = seen.thread;
  }
  CHECK(std::find(ids.begin(), ids.end(), teller) != ids.end());
  checkDeathMessage(seen,
      device,
      fault ? "killed by signal 11" : "killed by signal 9",
      nullptr,
      teller);
  CHECK(!processExists(host));
  checkDeadDevice(scene);

  scene.release();
  anariRelease(device, device);
  CHECK(seen.fatal == 1);
  CHECK(seen.afterDeath == 0);
}

int main()
{
  Seen seen;
  ANARILibrary library = anariLoadLibrary("guard", Seen::callback, &seen);
  CHECK(library);

  for (int repeat = 0; repeat < 3; repeat++) {
    // In the middle of the threads' calls: the scene makes four commits, and
    // each thread one in every round.
    run(library, seen, "commitParameters:150:before:segv");
    // With a reply owed to one of them
    run(library, seen, "getProperty:100:after:segv");
    run(library, seen, nullptr);
  }

  anariUnloadLibrary(library);
  std::printf("passed\n");
  return 0;
}
