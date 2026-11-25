// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Several threads of an application may work on different objects of one
// guard device at once, as ANARI_KHR_DEVICE_SYNCHRONIZATION says. Each
// thread's calls reach the wrapped device complete and in the order that
// thread made them, and a scene whose surfaces were built by several threads
// renders the same pixels as one built by a single thread in-process.
// anariRenderFrame and the calls on the device itself are made while no other
// thread is calling, as the extension requires.

#include "Recording.h"

#include <atomic>
#include <map>
#include <thread>

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

// Started together, so that the threads do overlap.
template <typename F>
static void runThreads(F &&body)
{
  std::atomic<bool> go{false};
  std::vector<std::thread> threads;
  for (int t = 0; t < THREADS; t++) {
    threads.emplace_back([&, t] {
      while (!go)
        std::this_thread::yield();
      body(t);
    });
  }
  go = true;
  for (auto &thread : threads)
    thread.join();
}

// What the wrapped device is given ///////////////////////////////////////////

static const int ROUNDS = 5;

static std::string hex(const void *data, size_t size)
{
  std::string out = "[";
  char text[4];
  for (size_t i = 0; i < size; i++) {
    std::snprintf(
        text, sizeof(text), "%02x", static_cast<const uint8_t *>(data)[i]);
    out += text;
  }
  return out + "]";
}

// One thread's calls, and what the recording device should log for them.
// Objects are named in 'expected' by thread and by the order this thread
// created them in, "@<thread>:<n>", since the wrapped device's own numbering
// depends on how the threads interleave. What each object is created with is
// unlike anything another thread, or another round, creates.
static void makeCalls(ANARIDevice d, int t, std::vector<std::string> &expected)
{
  int created = 0;
  auto next = [&] {
    return "@" + std::to_string(t) + ":" + std::to_string(++created);
  };
  auto expect = [&](const std::string &line) { expected.push_back(line); };

  for (int round = 0; round < ROUNDS; round++) {
    const uint32_t tag = uint32_t(t) << 16 | uint32_t(round);

    const std::string subtype =
        "t" + std::to_string(t) + "." + std::to_string(round);
    ANARIGeometry geometry = anariNewGeometry(d, subtype.c_str());
    const std::string g = next();
    expect("newGeometry \"" + subtype + "\" -> " + g);
    anariSetParameter(d, geometry, "value", ANARI_UINT32, &tag);
    expect("setParameter " + g + " \"value\" ANARI_UINT32 " + hex(&tag, 4));

    // A shared array
    const uint32_t words[4] = {tag, tag, tag, tag};
    ANARIArray1D shared =
        anariNewArray1D(d, words, nullptr, nullptr, ANARI_UINT32, 4);
    const std::string s = next();
    expect(
        "newArray1D ANARI_UINT32 4 " + hex(words, sizeof(words)) + " -> " + s);
    anariSetParameter(d, geometry, "index", ANARI_ARRAY1D, &shared);
    expect("setParameter " + g + " \"index\" ANARI_ARRAY1D " + s);

    // A mapped parameter of 20 KB, which crosses in a region when the test
    // is run with an array ceiling below that
    uint64_t stride = 0;
    auto *positions = static_cast<float *>(anariMapParameterArray1D(
        d, geometry, "vertex.position", ANARI_FLOAT32, 5000, &stride));
    CHECK(positions && stride == sizeof(float));
    for (int i = 0; i < 5000; i++)
      positions[i] = float(tag) + float(i);
    const std::string sum = checksum(positions, 5000 * sizeof(float));
    anariUnmapParameterArray(d, geometry, "vertex.position");
    const std::string p = next();
    expect("newArray1D ANARI_FLOAT32 5000 " + sum + " -> " + p);
    expect("setParameter " + g + " \"vertex.position\" ANARI_ARRAY1D " + p);
    expect("release " + p);

    // A managed array, written through a mapping. Its length is its own.
    const size_t length = size_t(1 + t * ROUNDS + round);
    CHECK(length <= 32);
    ANARIArray1D managed =
        anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_UINT8, length);
    const std::string m = next();
    const std::vector<uint8_t> zeros(length, 0);
    expect("newArray1D ANARI_UINT8 " + std::to_string(length) + " "
        + hex(zeros.data(), length) + " -> " + m);
    auto *bytes = static_cast<uint8_t *>(anariMapArray(d, managed));
    CHECK(bytes);
    for (size_t i = 0; i < length; i++)
      bytes[i] = uint8_t(tag + i);
    const std::string written = hex(bytes, length);
    anariUnmapArray(d, managed);
    expect("unmapArray " + m + " " + written);

    // An array of this thread's objects
    const ANARIGeometry handles[2] = {geometry, geometry};
    ANARIArray1D objects =
        anariNewArray1D(d, handles, nullptr, nullptr, ANARI_GEOMETRY, 2);
    const std::string o = next();
    expect("newArray1D ANARI_GEOMETRY 2 [" + g + " " + g + "] -> " + o);

    anariCommitParameters(d, geometry);
    expect("commitParameters " + g);

    // A call that waits for the host's answer
    int32_t answer = 0;
    CHECK(anariGetProperty(
              d, geometry, "answer", ANARI_INT32, &answer, 4, ANARI_WAIT)
        == 1);
    CHECK(answer == 42);
    expect("getProperty " + g + " \"answer\" ANARI_INT32 size=4 wait=1 -> 1");

    for (ANARIObject object :
        {(ANARIObject)shared, (ANARIObject)managed, (ANARIObject)objects}) {
      anariRelease(d, object);
    }
    expect("release " + s);
    expect("release " + m);
    expect("release " + o);
    anariRelease(d, geometry);
    expect("release " + g);
  }
}

// The recording device's log, sorted by the thread whose objects each line
// names, with every object renamed to that thread's name for it. A line that
// names no object is the main thread's: -1.
static std::map<int, std::vector<std::string>> sortLog(
    const std::vector<std::string> &lines,
    const std::vector<std::vector<std::string>> &expected)
{
  // What each object is created with, to who made it and what they call it
  std::map<std::string, std::string> creations;
  for (auto &thread : expected) {
    for (auto &line : thread) {
      const auto arrow = line.rfind(" -> @");
      if (arrow != std::string::npos)
        CHECK(creations.emplace(line.substr(0, arrow), line.substr(arrow + 4))
                  .second);
    }
  }

  std::map<std::string, std::string> names; // the wrapped device's, to ours
  std::map<int, std::vector<std::string>> sorted;
  for (const std::string &line : lines) {
    std::string renamed;
    int thread = -1;
    for (size_t i = 0; i < line.size();) {
      if (line[i] != '#') {
        renamed += line[i++];
        continue;
      }
      size_t end = i + 1;
      while (end < line.size() && std::isdigit((unsigned char)line[end]))
        end++;
      const std::string theirs = line.substr(i, end - i);
      i = end;
      auto known = names.find(theirs);
      if (known == names.end()) {
        // Only where an object is created is its name new, and that is the
        // last thing on the line.
        CHECK(i == line.size() && renamed.size() > 4);
        auto made = creations.find(renamed.substr(0, renamed.size() - 4));
        if (made == creations.end()) {
          std::fprintf(stderr,
              "FAILED: the wrapped device was given a creation no thread "
              "made:\n  %s\n",
              line.c_str());
          std::exit(1);
        }
        known = names.emplace(theirs, made->second).first;
      }
      // "@<thread>:<n>"
      const int owner = std::atoi(known->second.c_str() + 1);
      if (thread != -1 && owner != thread) {
        std::fprintf(stderr,
            "FAILED: one call names the objects of two threads:\n  %s\n",
            line.c_str());
        std::exit(1);
      }
      thread = owner;
      renamed += known->second;
    }
    sorted[thread].push_back(renamed);
  }
  return sorted;
}

static void checkCallsArrive(ANARILibrary library)
{
  char path[] = "recorder-log-XXXXXX";
  const int fd = mkstemp(path);
  CHECK(fd >= 0);
  close(fd);
  setenv("GUARD_RECORDER_LOG", path, 1);

  ANARIDevice d = anariNewDevice(library, "guard_recorder");
  CHECK(d);
  checkSynchronized(d);

  std::vector<std::vector<std::string>> expected(THREADS);
  runThreads([&](int t) { makeCalls(d, t, expected[size_t(t)]); });

  anariRelease(d, d);

  auto actual = sortLog(readLines(path), expected);
  for (int t = 0; t < THREADS; t++) {
    const auto &e = expected[size_t(t)];
    const auto &a = actual[t];
    for (size_t i = 0; i < e.size() || i < a.size(); i++) {
      const std::string el = i < e.size() ? e[i] : "<nothing>";
      const std::string al = i < a.size() ? a[i] : "<nothing>";
      if (el != al) {
        std::fprintf(stderr,
            "FAILED: thread %d's call %zu\n  expected: %s\n  actual:   %s\n",
            t,
            i + 1,
            el.c_str(),
            al.c_str());
        std::exit(1);
      }
    }
  }
  const std::vector<std::string> ours = {
      "getProperty device \"extension\" ANARI_STRING_LIST size=8 wait=1 -> 1",
      "release device"};
  CHECK(actual[-1] == ours);
  unlink(path);
}

// What is rendered ///////////////////////////////////////////////////////////

static const size_t SURFACES = 96;

// A surface that is one triangle in cell 'index' of a grid over the image,
// its colour following from 'index' too.
static ANARISurface makeSurface(ANARIDevice d, size_t index)
{
  size_t side = 1;
  while (side * side < SURFACES)
    side++;
  const float step = 2.f / float(side);
  const float x = -1.f + step * float(index % side);
  const float y = -1.f + step * float(index / side);
  const float z = -3.f;
  const float positions[9] = {x + .1f * step,
      y + .1f * step,
      z,
      x + .9f * step,
      y + .1f * step,
      z,
      x + .5f * step,
      y + .9f * step,
      z};
  float colors[12];
  for (int v = 0; v < 3; v++) {
    // never the background's black
    colors[4 * v + 0] = .3f + .7f * float((index * 37) % 11) / 10.f;
    colors[4 * v + 1] = .3f + .7f * float((index * 53) % 7) / 6.f;
    colors[4 * v + 2] = .3f + .7f * float((index * 71) % 5) / 4.f;
    colors[4 * v + 3] = 1.f;
  }

  // One array of each kind: mapped, shared and managed.
  ANARIGeometry geometry = anariNewGeometry(d, "triangle");
  uint64_t stride = 0;
  void *mem = anariMapParameterArray1D(
      d, geometry, "vertex.position", ANARI_FLOAT32_VEC3, 3, &stride);
  CHECK(mem);
  std::memcpy(mem, positions, sizeof(positions));
  anariUnmapParameterArray(d, geometry, "vertex.position");
  ANARIArray1D colorArray =
      anariNewArray1D(d, colors, nullptr, nullptr, ANARI_FLOAT32_VEC4, 3);
  anariSetParameter(d, geometry, "vertex.color", ANARI_ARRAY1D, &colorArray);
  anariRelease(d, colorArray);
  ANARIArray1D indexArray =
      anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_UINT32_VEC3, 1);
  auto *indices = static_cast<uint32_t *>(anariMapArray(d, indexArray));
  CHECK(indices);
  indices[0] = 0;
  indices[1] = 1;
  indices[2] = 2;
  anariUnmapArray(d, indexArray);
  anariSetParameter(d, geometry, "primitive.index", ANARI_ARRAY1D, &indexArray);
  anariRelease(d, indexArray);
  anariCommitParameters(d, geometry);

  ANARIMaterial material = anariNewMaterial(d, "matte");
  anariSetParameter(d, material, "color", ANARI_STRING, "color");
  anariCommitParameters(d, material);

  ANARISurface surface = anariNewSurface(d);
  anariSetParameter(d, surface, "geometry", ANARI_GEOMETRY, &geometry);
  anariSetParameter(d, surface, "material", ANARI_MATERIAL, &material);
  anariCommitParameters(d, surface);
  anariRelease(d, geometry);
  anariRelease(d, material);
  return surface;
}

// The scene's picture. Its surfaces are made by one thread, or by THREADS
// threads at once that each make their share; everything else is the calling
// thread's, once they are done.
static std::vector<uint8_t> render(ANARIDevice d, bool threaded)
{
  std::vector<ANARISurface> surfaces(SURFACES);
  if (threaded) {
    runThreads([&](int t) {
      for (size_t i = size_t(t); i < SURFACES; i += THREADS)
        surfaces[i] = makeSurface(d, i);
    });
  } else {
    for (size_t i = 0; i < SURFACES; i++)
      surfaces[i] = makeSurface(d, i);
  }

  ANARIArray1D array = anariNewArray1D(
      d, surfaces.data(), nullptr, nullptr, ANARI_SURFACE, SURFACES);
  ANARIWorld world = anariNewWorld(d);
  anariSetParameter(d, world, "surface", ANARI_ARRAY1D, &array);
  anariCommitParameters(d, world);
  anariRelease(d, array);
  for (ANARISurface s : surfaces)
    anariRelease(d, s);

  ANARICamera camera = anariNewCamera(d, "orthographic");
  const float position[3] = {0.f, 0.f, 0.f};
  const float direction[3] = {0.f, 0.f, -1.f};
  const float up[3] = {0.f, 1.f, 0.f};
  const float aspect = 1.f;
  const float height = 2.f;
  anariSetParameter(d, camera, "position", ANARI_FLOAT32_VEC3, position);
  anariSetParameter(d, camera, "direction", ANARI_FLOAT32_VEC3, direction);
  anariSetParameter(d, camera, "up", ANARI_FLOAT32_VEC3, up);
  anariSetParameter(d, camera, "aspect", ANARI_FLOAT32, &aspect);
  anariSetParameter(d, camera, "height", ANARI_FLOAT32, &height);
  anariCommitParameters(d, camera);

  ANARIRenderer renderer = anariNewRenderer(d, "default");
  const float ambient = 1.f;
  anariSetParameter(d, renderer, "ambientRadiance", ANARI_FLOAT32, &ambient);
  anariCommitParameters(d, renderer);

  ANARIFrame frame = anariNewFrame(d);
  const uint32_t size[2] = {256, 256};
  const ANARIDataType colorType = ANARI_UFIXED8_RGBA_SRGB;
  anariSetParameter(d, frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(d, frame, "channel.color", ANARI_DATA_TYPE, &colorType);
  anariSetParameter(d, frame, "camera", ANARI_CAMERA, &camera);
  anariSetParameter(d, frame, "renderer", ANARI_RENDERER, &renderer);
  anariSetParameter(d, frame, "world", ANARI_WORLD, &world);
  anariCommitParameters(d, frame);

  anariRenderFrame(d, frame);
  CHECK(anariFrameReady(d, frame, ANARI_WAIT) == 1);
  uint32_t w = 0, h = 0;
  ANARIDataType type = ANARI_UNKNOWN;
  auto *pixels = static_cast<const uint8_t *>(
      anariMapFrame(d, frame, "channel.color", &w, &h, &type));
  CHECK(pixels && w == 256 && h == 256);
  std::vector<uint8_t> image(pixels, pixels + size_t(w) * h * 4);
  anariUnmapFrame(d, frame, "channel.color");

  anariRelease(d, frame);
  anariRelease(d, renderer);
  anariRelease(d, camera);
  anariRelease(d, world);
  return image;
}

static std::atomic<int> errors{0};

static void onStatus(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_ERROR) {
    errors++;
    std::fprintf(stderr, "[status] %s\n", message);
  }
}

// The picture a single thread gets from the wrapped device in-process.
[[maybe_unused]] static std::vector<uint8_t> renderInProcess()
{
  const char *wrapped = std::getenv("ANARI_GUARD_WRAPPED_DEVICE");
  CHECK(wrapped);
  ANARILibrary directLibrary = anariLoadLibrary(wrapped, onStatus, nullptr);
  CHECK(directLibrary);
  ANARIDevice direct = anariNewDevice(directLibrary, "default");
  CHECK(direct);
  const std::vector<uint8_t> expected = render(direct, false);
  anariRelease(direct, direct);
  // Left loaded: unloading a device's library can take its threads' code
  // from under them (helide's TBB workers may still be starting).
  (void)directLibrary;
  return expected;
}

static void checkPixels(ANARILibrary guardLibrary)
{
  ANARIDevice guarded = anariNewDevice(guardLibrary, "default");
  CHECK(guarded);
  checkSynchronized(guarded);

#if defined(GUARD_SANITIZE_THREAD)
  // A wrapped device in this process, not built for ThreadSanitizer, would
  // be reported on for what it does inside itself. The single thread goes
  // through the guard device instead.
  const std::vector<uint8_t> expected = render(guarded, false);
#else
  const std::vector<uint8_t> expected = renderInProcess();
#endif

  // The picture shows every surface, so one that went missing, or was given
  // another thread's vertices or colours, would show.
  size_t lit = 0;
  for (size_t i = 0; i < expected.size(); i += 4)
    lit += std::memcmp(&expected[i], &expected[0], 4) != 0;
  size_t side = 1;
  while (side * side < SURFACES)
    side++;
  const double cell = 256. / double(side);
  CHECK(double(lit) > .9 * .5 * (.8 * cell) * (.8 * cell) * double(SURFACES));

  for (int repeat = 0; repeat < 5; repeat++)
    CHECK(render(guarded, true) == expected);
  anariRelease(guarded, guarded);
  CHECK(errors == 0);
}

int main()
{
  ANARILibrary library = anariLoadLibrary("guard", onStatus, nullptr);
  CHECK(library);

  checkCallsArrive(library);
  checkPixels(library);

  anariUnloadLibrary(library);
  std::printf("passed\n");
  return 0;
}
