// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Every ANARI call an application makes through the guard device reaches the
// wrapped device with the same arguments, and every answer comes back
// unchanged: each message of the protocol, there and back.

#include "Recording.h"

#include <atomic>

using namespace guard_test;

static std::atomic<int> completions{0};
static std::atomic<int> deleterRuns{0};

static void onCompletion(const void *, ANARIDevice, ANARIFrame)
{
  completions++;
}

static void onDelete(const void *, const void *)
{
  deleterRuns++;
}

// The recording device's pixels for 'channel.color' after 'renders' renders.
static std::vector<uint8_t> colorPattern(size_t pixels, uint32_t renders)
{
  std::vector<uint8_t> color(pixels * 4);
  for (size_t i = 0; i < color.size(); i++)
    color[i] = uint8_t(i * 31 + 7 * renders);
  return color;
}

static std::string listText(const std::string &item, size_t count)
{
  std::string out = "[";
  for (size_t i = 0; i < count; i++)
    out += (i ? " " : "") + item;
  return out + "]";
}

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

  // A round trip, after which the wrapped device has been given every call
  // made so far. A region is memory the host reads when it gets to the call,
  // so a test that writes to one again waits for the host first.
  auto sync = [&] {
    int32_t value = 0;
    CHECK(anariGetProperty(d, d, "answer", ANARI_INT32, &value, 4, ANARI_WAIT));
    expect("getProperty device \"answer\" ANARI_INT32 size=4 wait=1 -> 1");
  };

  // Object creation //////////////////////////////////////////////////////////

  ANARIGeometry geometry = anariNewGeometry(d, "triangle");
  expect("newGeometry \"triangle\" -> #1");
  ANARIMaterial material = anariNewMaterial(d, "matte");
  expect("newMaterial \"matte\" -> #2");
  ANARISampler sampler = anariNewSampler(d, "image2D");
  expect("newSampler \"image2D\" -> #3");
  ANARISurface surface = anariNewSurface(d);
  expect("newSurface -> #4");
  ANARISpatialField field = anariNewSpatialField(d, "structuredRegular");
  expect("newSpatialField \"structuredRegular\" -> #5");
  ANARIVolume volume = anariNewVolume(d, "transferFunction1D");
  expect("newVolume \"transferFunction1D\" -> #6");
  ANARILight light = anariNewLight(d, "point");
  expect("newLight \"point\" -> #7");
  ANARIGroup group = anariNewGroup(d);
  expect("newGroup -> #8");
  ANARIInstance instance = anariNewInstance(d, "transform");
  expect("newInstance \"transform\" -> #9");
  ANARIWorld world = anariNewWorld(d);
  expect("newWorld -> #10");
  ANARICamera camera = anariNewCamera(d, "perspective");
  expect("newCamera \"perspective\" -> #11");
  ANARIRenderer renderer = anariNewRenderer(d, "default");
  expect("newRenderer \"default\" -> #12");
  ANARIFrame frame = anariNewFrame(d);
  expect("newFrame -> #13");
  ANARIObject object = anariNewObject(d, "kind", "sub");
  expect("newObject \"kind\" \"sub\" -> #14");
  CHECK(geometry && material && sampler && surface && field && volume && light
      && group && instance && world && camera && renderer && frame && object);

  // An object the wrapped device refuses to create is still a handle here,
  // and calls on it go nowhere.
  ANARIGeometry refused = anariNewGeometry(d, "refused");
  expect("newGeometry \"refused\" -> null");
  CHECK(refused);
  anariCommitParameters(d, refused);
  anariRelease(d, refused);

  // Parameters ///////////////////////////////////////////////////////////////

  const float radius = 1.5f;
  anariSetParameter(d, geometry, "radius", ANARI_FLOAT32, &radius);
  expect("setParameter #1 \"radius\" ANARI_FLOAT32 [0000c03f]");
  anariSetParameter(d, surface, "geometry", ANARI_GEOMETRY, &geometry);
  expect("setParameter #4 \"geometry\" ANARI_GEOMETRY #1");
  const ANARIMaterial none = nullptr;
  anariSetParameter(d, surface, "material", ANARI_MATERIAL, &none);
  expect("setParameter #4 \"material\" ANARI_MATERIAL null");
  anariSetParameter(d, material, "name", ANARI_STRING, "red");
  expect("setParameter #2 \"name\" ANARI_STRING \"red\"");
  const char *names[] = {"a", "bc", nullptr};
  anariSetParameter(d, renderer, "names", ANARI_STRING_LIST, names);
  expect("setParameter #12 \"names\" ANARI_STRING_LIST [\"a\" \"bc\"]");
  float transform[16];
  for (int i = 0; i < 16; i++)
    transform[i] = float(i) * 0.25f;
  anariSetParameter(d, instance, "transform", ANARI_FLOAT32_MAT4, transform);
  expect("setParameter #9 \"transform\" ANARI_FLOAT32_MAT4 "
      + checksum(transform, sizeof(transform)));
  const int32_t three = 3;
  anariSetParameter(d, d, "deviceIndex", ANARI_INT32, &three);
  expect("setParameter device \"deviceIndex\" ANARI_INT32 [03000000]");
  anariCommitParameters(d, d);
  expect("commitParameters device");
  anariUnsetParameter(d, geometry, "radius");
  expect("unsetParameter #1 \"radius\"");
  anariUnsetAllParameters(d, material);
  expect("unsetAllParameters #2");
  anariCommitParameters(d, geometry);
  expect("commitParameters #1");
  anariRetain(d, geometry);
  expect("retain #1");
  anariRelease(d, geometry);
  expect("release #1");

  // Arrays: contents inline, in a region, and none yet. Which of the first
  // two depends on the array ceiling; what the wrapped device is given does
  // not, and this test is run at two ceilings.

  float small[3] = {1.f, 2.f, 3.f};
  ANARIArray1D sharedSmall =
      anariNewArray1D(d, small, onDelete, nullptr, ANARI_FLOAT32, 3);
  expect("newArray1D ANARI_FLOAT32 3 [0000803f0000004000004040] -> #15");
  small[0] = 4.f;
  CHECK(anariMapArray(d, sharedSmall) == small);
  anariUnmapArray(d, sharedSmall);
  expect("unmapArray #15 [000080400000004000004040]");

  ANARIArray2D managedSmall =
      anariNewArray2D(d, nullptr, nullptr, nullptr, ANARI_UINT8, 2, 3);
  expect("newArray2D ANARI_UINT8 2x3 [000000000000] -> #16");
  auto *bytes = static_cast<uint8_t *>(anariMapArray(d, managedSmall));
  CHECK(bytes);
  for (int i = 0; i < 6; i++)
    bytes[i] = uint8_t(i + 1);
  anariUnmapArray(d, managedSmall);
  expect("unmapArray #16 [010203040506]");
  sync();
  bytes = static_cast<uint8_t *>(anariMapArray(d, managedSmall));
  CHECK(bytes && bytes[0] == 1 && bytes[5] == 6);
  bytes[0] = 9;
  anariUnmapArray(d, managedSmall);
  expect("unmapArray #16 [090203040506]");

  std::vector<uint8_t> large(32 * 32 * 32);
  for (size_t i = 0; i < large.size(); i++)
    large[i] = uint8_t(i * 7 + 3);
  ANARIArray3D sharedLarge = anariNewArray3D(
      d, large.data(), nullptr, nullptr, ANARI_UINT8, 32, 32, 32);
  expect("newArray3D ANARI_UINT8 32x32x32 "
      + checksum(large.data(), large.size()) + " -> #17");
  sync();
  CHECK(anariMapArray(d, sharedLarge) == large.data());
  for (size_t i = 0; i < large.size(); i++)
    large[i] = uint8_t(i * 5 + 1);
  anariUnmapArray(d, sharedLarge);
  expect("unmapArray #17 " + checksum(large.data(), large.size()));
  // Written a third and a fourth time: still the one array, with what the
  // application wrote each time.
  for (int write = 3; write <= 4; write++) {
    sync();
    CHECK(anariMapArray(d, sharedLarge) == large.data());
    for (size_t i = 0; i < large.size(); i++)
      large[i] = uint8_t(i * 3 + write);
    anariUnmapArray(d, sharedLarge);
    expect("unmapArray #17 " + checksum(large.data(), large.size()));
  }

  const std::vector<uint32_t> zeros(5000, 0);
  ANARIArray1D managedLarge =
      anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_UINT32, 5000);
  expect("newArray1D ANARI_UINT32 5000 "
      + checksum(zeros.data(), zeros.size() * 4) + " -> #18");
  sync();
  auto *words = static_cast<uint32_t *>(anariMapArray(d, managedLarge));
  CHECK(words);
  for (uint32_t i = 0; i < 5000; i++)
    words[i] = i * i;
  const std::string written = checksum(words, 5000 * 4);
  anariUnmapArray(d, managedLarge);
  expect("unmapArray #18 " + written);
  // Mapped again it holds what was written, wherever it now is, and what is
  // changed from there reaches the one array.
  for (uint32_t write = 2; write <= 4; write++) {
    sync();
    words = static_cast<uint32_t *>(anariMapArray(d, managedLarge));
    CHECK(words);
    for (uint32_t i = 0; i < 5000; i++)
      CHECK(words[i] == i * i + (write - 2) * 7);
    for (uint32_t i = 0; i < 5000; i++)
      words[i] += 7;
    const std::string rewritten = checksum(words, 5000 * 4);
    anariUnmapArray(d, managedLarge);
    expect("unmapArray #18 " + rewritten);
  }

  // Object arrays: handles here are the wrapped device's own handles there.
  ANARIObject pair[2] = {surface, geometry};
  ANARIArray1D sharedObjects =
      anariNewArray1D(d, pair, nullptr, nullptr, ANARI_OBJECT, 2);
  expect("newArray1D ANARI_OBJECT 2 [#4 #1] -> #19");
  ANARIArray1D managedObjects =
      anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_SURFACE, 2);
  expect("newArray1D ANARI_SURFACE 2 [null null] -> #20");
  auto *handles = static_cast<ANARIObject *>(anariMapArray(d, managedObjects));
  CHECK(handles);
  handles[0] = surface;
  handles[1] = nullptr;
  anariUnmapArray(d, managedObjects);
  expect("unmapArray #20 [#4 null]");
  std::vector<ANARIObject> many(2048, surface);
  ANARIArray1D largeObjects = anariNewArray1D(
      d, many.data(), nullptr, nullptr, ANARI_SURFACE, many.size());
  expect("newArray1D ANARI_SURFACE 2048 " + listText("#4", 2048) + " -> #21");
  sync();
  many[0] = nullptr;
  anariMapArray(d, largeObjects);
  anariUnmapArray(d, largeObjects);
  expect("unmapArray #21 [null " + listText("#4", 2047).substr(1));

  // Parameter mappings: an array, a parameter set to it, and its release.
  uint64_t stride = 0;
  auto *positions = static_cast<float *>(anariMapParameterArray1D(
      d, geometry, "vertex.position", ANARI_FLOAT32_VEC3, 2, &stride));
  CHECK(positions && stride == 12);
  for (int i = 0; i < 6; i++)
    positions[i] = float(i);
  anariUnmapParameterArray(d, geometry, "vertex.position");
  expect(
      "newArray1D ANARI_FLOAT32_VEC3 2 "
      "[000000000000803f0000004000004040000080400000a040] -> #22");
  expect("setParameter #1 \"vertex.position\" ANARI_ARRAY1D #22");
  expect("release #22");

  auto *image = static_cast<uint8_t *>(anariMapParameterArray2D(
      d, sampler, "image", ANARI_UINT8, 100, 200, &stride));
  CHECK(image && stride == 1);
  for (size_t i = 0; i < 20000; i++)
    image[i] = uint8_t(i * 3);
  const std::string imageSum = checksum(image, 20000);
  anariUnmapParameterArray(d, sampler, "image");
  expect("newArray2D ANARI_UINT8 100x200 " + imageSum + " -> #23");
  expect("setParameter #3 \"image\" ANARI_ARRAY2D #23");
  expect("release #23");

  auto *cells = static_cast<uint8_t *>(anariMapParameterArray3D(
      d, field, "data", ANARI_UINT8, 2, 2, 2, &stride));
  CHECK(cells && stride == 1);
  for (int i = 0; i < 8; i++)
    cells[i] = uint8_t(0x10 + i);
  anariUnmapParameterArray(d, field, "data");
  expect("newArray3D ANARI_UINT8 2x2x2 [1011121314151617] -> #24");
  expect("setParameter #5 \"data\" ANARI_ARRAY3D #24");
  expect("release #24");

  // Properties ///////////////////////////////////////////////////////////////

  int32_t answer = 0;
  CHECK(
      anariGetProperty(d, world, "answer", ANARI_INT32, &answer, 4, ANARI_WAIT)
      == 1);
  CHECK(answer == 42);
  expect("getProperty #10 \"answer\" ANARI_INT32 size=4 wait=1 -> 1");
  float bounds[6] = {};
  CHECK(anariGetProperty(
            d, world, "bounds", ANARI_FLOAT32_BOX3, bounds, 24, ANARI_NO_WAIT)
      == 1);
  for (int i = 0; i < 6; i++)
    CHECK(bounds[i] == float(i + 1));
  expect("getProperty #10 \"bounds\" ANARI_FLOAT32_BOX3 size=24 wait=0 -> 1");
  answer = 1234;
  CHECK(
      anariGetProperty(d, d, "nothing", ANARI_INT32, &answer, 4, ANARI_NO_WAIT)
      == 0);
  CHECK(answer == 1234);
  expect("getProperty device \"nothing\" ANARI_INT32 size=4 wait=0 -> 0");

  // Introspection ////////////////////////////////////////////////////////////

  const char **subtypes = anariGetObjectSubtypes(d, ANARI_RENDERER);
  expect("getObjectSubtypes ANARI_RENDERER");
  CHECK(subtypes && std::string(subtypes[0]) == "default"
      && std::string(subtypes[1]) == "fancy" && !subtypes[2]);
  CHECK(anariGetObjectSubtypes(d, ANARI_LIGHT) == nullptr);
  expect("getObjectSubtypes ANARI_LIGHT");

  auto *description = static_cast<const char *>(anariGetObjectInfo(
      d, ANARI_RENDERER, "fancy", "description", ANARI_STRING));
  expect("getObjectInfo ANARI_RENDERER \"fancy\" \"description\" ANARI_STRING");
  CHECK(description && std::string(description) == "a fancy thing");
  auto *parameters = static_cast<const ANARIParameter *>(anariGetObjectInfo(
      d, ANARI_GEOMETRY, "triangle", "parameter", ANARI_PARAMETER_LIST));
  expect(
      "getObjectInfo ANARI_GEOMETRY \"triangle\" \"parameter\" "
      "ANARI_PARAMETER_LIST");
  CHECK(parameters && std::string(parameters[0].name) == "color"
      && parameters[0].type == ANARI_FLOAT32_VEC4
      && std::string(parameters[1].name) == "radius"
      && parameters[1].type == ANARI_FLOAT32 && !parameters[2].name);
  auto *types = static_cast<const ANARIDataType *>(anariGetObjectInfo(
      d, ANARI_GEOMETRY, "triangle", "types", ANARI_DATA_TYPE_LIST));
  expect(
      "getObjectInfo ANARI_GEOMETRY \"triangle\" \"types\" "
      "ANARI_DATA_TYPE_LIST");
  CHECK(types && types[0] == ANARI_FLOAT32 && types[1] == ANARI_UINT8
      && types[2] == ANARI_UNKNOWN);
  CHECK(
      anariGetObjectInfo(d, ANARI_GEOMETRY, "triangle", "nothing", ANARI_INT32)
      == nullptr);
  expect("getObjectInfo ANARI_GEOMETRY \"triangle\" \"nothing\" ANARI_INT32");

  auto *fallback = static_cast<const float *>(anariGetParameterInfo(d,
      ANARI_GEOMETRY,
      "triangle",
      "radius",
      ANARI_FLOAT32,
      "default",
      ANARI_FLOAT32));
  expect(
      "getParameterInfo ANARI_GEOMETRY \"triangle\" \"radius\" "
      "ANARI_FLOAT32 \"default\" ANARI_FLOAT32");
  CHECK(fallback && *fallback == 0.5f);
  auto *about = static_cast<const char *>(anariGetParameterInfo(d,
      ANARI_GEOMETRY,
      "triangle",
      "radius",
      ANARI_FLOAT32,
      "description",
      ANARI_STRING));
  expect(
      "getParameterInfo ANARI_GEOMETRY \"triangle\" \"radius\" "
      "ANARI_FLOAT32 \"description\" ANARI_STRING");
  CHECK(about && std::string(about) == "radius of triangle");

  // Asked again, an introspection query is answered without asking the
  // wrapped device, with the same pointer.
  CHECK(anariGetObjectSubtypes(d, ANARI_RENDERER) == subtypes);
  CHECK(anariGetObjectInfo(
            d, ANARI_RENDERER, "fancy", "description", ANARI_STRING)
      == description);
  CHECK(anariGetParameterInfo(d,
            ANARI_GEOMETRY,
            "triangle",
            "radius",
            ANARI_FLOAT32,
            "default",
            ANARI_FLOAT32)
      == fallback);
  CHECK(anariGetObjectSubtypes(d, ANARI_LIGHT) == nullptr);

  // Frames ///////////////////////////////////////////////////////////////////

  uint32_t size[2] = {4, 3};
  anariSetParameter(d, frame, "size", ANARI_UINT32_VEC2, size);
  expect("setParameter #13 \"size\" ANARI_UINT32_VEC2 [0400000003000000]");
  ANARIFrameCompletionCallback callback = onCompletion;
  anariSetParameter(d,
      frame,
      "frameCompletionCallback",
      ANARI_FRAME_COMPLETION_CALLBACK,
      &callback);
  anariCommitParameters(d, frame);
  expect("commitParameters #13");
  anariRenderFrame(d, frame);
  expect("renderFrame #13");
  CHECK(anariFrameReady(d, frame, ANARI_WAIT) == 1);
  CHECK(completions == 1);

  // A small channel comes back inline.
  uint32_t width = 0, height = 0;
  ANARIDataType pixelType = ANARI_UNKNOWN;
  auto *color = static_cast<const uint8_t *>(
      anariMapFrame(d, frame, "channel.color", &width, &height, &pixelType));
  expect("frameBufferMap #13 \"channel.color\"");
  expect("frameBufferUnmap #13 \"channel.color\"");
  CHECK(color && width == 4 && height == 3 && pixelType == ANARI_UFIXED8_VEC4);
  CHECK(std::memcmp(color, colorPattern(12, 1).data(), 48) == 0);
  anariUnmapFrame(d, frame, "channel.color");

  auto *depth = static_cast<const float *>(
      anariMapFrame(d, frame, "channel.depth", &width, &height, &pixelType));
  expect("frameBufferMap #13 \"channel.depth\"");
  expect("frameBufferUnmap #13 \"channel.depth\"");
  CHECK(depth && width == 4 && height == 3 && pixelType == ANARI_FLOAT32);
  for (int i = 0; i < 12; i++)
    CHECK(depth[i] == 0.5f * float(i) + 1.f);
  anariUnmapFrame(d, frame, "channel.depth");

  // A channel the wrapped device does not have.
  width = height = 77;
  CHECK(anariMapFrame(d, frame, "channel.nothing", &width, &height, &pixelType)
      == nullptr);
  expect("frameBufferMap #13 \"channel.nothing\"");
  expect("frameBufferUnmap #13 \"channel.nothing\"");
  CHECK(width == 0 && height == 0 && pixelType == ANARI_UNKNOWN);

  // A large channel comes back in a region: a new one the first time, the
  // same one the next.
  size[0] = 80;
  size[1] = 60;
  anariSetParameter(d, frame, "size", ANARI_UINT32_VEC2, size);
  expect("setParameter #13 \"size\" ANARI_UINT32_VEC2 [500000003c000000]");
  anariCommitParameters(d, frame);
  expect("commitParameters #13");
  for (uint32_t renders = 2; renders <= 3; renders++) {
    anariRenderFrame(d, frame);
    expect("renderFrame #13");
    CHECK(anariFrameReady(d, frame, ANARI_WAIT) == 1);
    color = static_cast<const uint8_t *>(
        anariMapFrame(d, frame, "channel.color", &width, &height, &pixelType));
    expect("frameBufferMap #13 \"channel.color\"");
    expect("frameBufferUnmap #13 \"channel.color\"");
    CHECK(color && width == 80 && height == 60);
    CHECK(std::memcmp(color, colorPattern(4800, renders).data(), 19200) == 0);
    anariUnmapFrame(d, frame, "channel.color");
  }
  CHECK(completions == 3);
  anariDiscardFrame(d, frame);
  expect("discardFrame #13");

  // Status messages //////////////////////////////////////////////////////////

  anariSetParameter(d, camera, "emitStatus", ANARI_STRING, "from the camera");
  expect("setParameter #11 \"emitStatus\" ANARI_STRING \"from the camera\"");
  anariSetParameter(d, d, "emitStatus", ANARI_STRING, "from the device");
  expect("setParameter device \"emitStatus\" ANARI_STRING \"from the device\"");
  sync();
  CHECK(
      waitFor([&] { return status.matching("from the device").size() == 1; }));
  {
    const auto fromCamera = status.matching("from the camera");
    CHECK(fromCamera.size() == 1);
    CHECK(fromCamera[0].device == d);
    CHECK(fromCamera[0].source == (ANARIObject)camera);
    CHECK(fromCamera[0].sourceType == ANARI_CAMERA);
    CHECK(fromCamera[0].severity == ANARI_SEVERITY_WARNING);
    CHECK(fromCamera[0].code == ANARI_STATUS_INVALID_OPERATION);
    CHECK(fromCamera[0].text == "from the camera");
    const auto fromDevice = status.matching("from the device");
    CHECK(fromDevice[0].device == d);
    CHECK(fromDevice[0].source == (ANARIObject)d);
    CHECK(fromDevice[0].sourceType == ANARI_DEVICE);
  }

  // Contents too large for the buffer of pipelined calls, which the adapter
  // writes to the channel from where they are: with calls waiting ahead of
  // them and calls behind.
  {
    std::vector<uint8_t> bulk(300000);
    for (size_t i = 0; i < bulk.size(); i++)
      bulk[i] = uint8_t(i * 11 + i / 256);
    anariCommitParameters(d, camera);
    expect("commitParameters #11");
    ANARIArray1D bulkShared = anariNewArray1D(
        d, bulk.data(), nullptr, nullptr, ANARI_UINT8, bulk.size());
    expect("newArray1D ANARI_UINT8 300000 " + checksum(bulk.data(), bulk.size())
        + " -> #25");
    ANARIArray1D bulkManaged =
        anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_UINT8, bulk.size());
    expect("newArray1D ANARI_UINT8 300000 "
        + checksum(std::vector<uint8_t>(bulk.size(), 0).data(), bulk.size())
        + " -> #26");
    sync(); // above the ceiling it is a region, which the host has yet to read
    std::memcpy(anariMapArray(d, bulkManaged), bulk.data(), bulk.size());
    anariUnmapArray(d, bulkManaged);
    expect("unmapArray #26 " + checksum(bulk.data(), bulk.size()));
    uint64_t bulkStride = 0;
    void *bulkMapped = anariMapParameterArray1D(
        d, sampler, "image", ANARI_UINT8, bulk.size(), &bulkStride);
    CHECK(bulkMapped && bulkStride == 1);
    std::memcpy(bulkMapped, bulk.data(), bulk.size());
    anariUnmapParameterArray(d, sampler, "image");
    expect("newArray1D ANARI_UINT8 300000 " + checksum(bulk.data(), bulk.size())
        + " -> #27");
    expect("setParameter #3 \"image\" ANARI_ARRAY1D #27");
    expect("release #27");
    anariCommitParameters(d, camera);
    expect("commitParameters #11");
    anariRelease(d, bulkShared);
    expect("release #25");
    anariRelease(d, bulkManaged);
    expect("release #26");
  }

  // Lifetime /////////////////////////////////////////////////////////////////

  CHECK(deleterRuns == 0);
  anariRelease(d, sharedSmall);
  expect("release #15");
  CHECK(deleterRuns == 1);
  anariRelease(d, frame);
  expect("release #13");
  anariRelease(d, d);
  expect("release device");
  anariUnloadLibrary(library);

  checkLog(log, expected);
  std::printf("passed\n");
  return 0;
}
