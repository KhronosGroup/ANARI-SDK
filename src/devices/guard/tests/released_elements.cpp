// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// An application may release a surface once it is in an array, and later map
// and unmap that array with the handle still in it. The array keeps the
// surface, so it is still drawn: the same pixels through the guard device as
// the wrapped device gives in-process, and no error.

#include "TestSupport.h"

#include <anari/anari.h>

#include <atomic>
#include <cstring>
#include <vector>

using namespace guard_test;

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

struct Case
{
  const char *name;
  bool managed; // else a shared array
  bool rewriteOthers; // at the second mapping, else nothing is changed
  size_t surfaces; // the first half are released
};

struct Images
{
  std::vector<uint8_t> first;
  std::vector<uint8_t> second; // after the release, the map and the unmap
};

// A surface that is one triangle in cell 'index' of a grid over the image,
// its colour following from 'index' too.
static ANARISurface makeSurface(ANARIDevice d, size_t index, size_t cells)
{
  size_t side = 1;
  while (side * side < cells)
    side++;
  const float step = 2.f / float(side);
  const float x = -1.f + step * float(index % side);
  const float y = -1.f + step * float((index / side) % side);
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

  ANARIGeometry geometry = anariNewGeometry(d, "triangle");
  uint64_t stride = 0;
  void *mem = anariMapParameterArray1D(
      d, geometry, "vertex.position", ANARI_FLOAT32_VEC3, 3, &stride);
  CHECK(mem);
  std::memcpy(mem, positions, sizeof(positions));
  anariUnmapParameterArray(d, geometry, "vertex.position");
  mem = anariMapParameterArray1D(
      d, geometry, "vertex.color", ANARI_FLOAT32_VEC4, 3, &stride);
  CHECK(mem);
  std::memcpy(mem, colors, sizeof(colors));
  anariUnmapParameterArray(d, geometry, "vertex.color");
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

static void appendFrame(
    ANARIDevice d, ANARIFrame frame, std::vector<uint8_t> &out)
{
  anariRenderFrame(d, frame);
  CHECK(anariFrameReady(d, frame, ANARI_WAIT) == 1);
  uint32_t w = 0, h = 0;
  ANARIDataType type = ANARI_UNKNOWN;
  auto *p = static_cast<const uint8_t *>(
      anariMapFrame(d, frame, "channel.color", &w, &h, &type));
  CHECK(p && w > 0 && h > 0);
  out.insert(out.end(), p, p + size_t(w) * h * 4);
  anariUnmapFrame(d, frame, "channel.color");
}

static Images render(ANARIDevice d, const Case &c)
{
  const size_t n = c.surfaces;
  const size_t released = n / 2;
  const size_t cells = 2 * n; // the second half is for the replacements

  std::vector<ANARISurface> surfaces(n);
  for (size_t i = 0; i < n; i++)
    surfaces[i] = makeSurface(d, i, cells);

  // The array's elements: the application's memory when it is shared.
  std::vector<ANARISurface> memory = surfaces;
  ANARIArray1D array = nullptr;
  if (c.managed) {
    array = anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_SURFACE, n);
    void *mem = anariMapArray(d, array);
    CHECK(mem);
    std::memcpy(mem, surfaces.data(), n * sizeof(ANARISurface));
    anariUnmapArray(d, array);
  } else {
    array =
        anariNewArray1D(d, memory.data(), nullptr, nullptr, ANARI_SURFACE, n);
  }

  ANARIGroup group = anariNewGroup(d);
  anariSetParameter(d, group, "surface", ANARI_ARRAY1D, &array);
  anariCommitParameters(d, group);
  ANARIInstance instance = anariNewInstance(d, "transform");
  anariSetParameter(d, instance, "group", ANARI_GROUP, &group);
  anariCommitParameters(d, instance);
  ANARIWorld world = anariNewWorld(d);
  ANARIArray1D instances =
      anariNewArray1D(d, &instance, nullptr, nullptr, ANARI_INSTANCE, 1);
  anariSetParameter(d, world, "instance", ANARI_ARRAY1D, &instances);
  anariRelease(d, instances);
  anariCommitParameters(d, world);

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

  Images images;
  appendFrame(d, frame, images.first);

  // The application is done with the first half of the surfaces. Their
  // handles stay where they are in the array.
  for (size_t i = 0; i < released; i++)
    anariRelease(d, surfaces[i]);

  auto *mem = static_cast<ANARISurface *>(anariMapArray(d, array));
  CHECK(mem);
  if (!c.managed)
    CHECK(mem == memory.data());
  CHECK(std::memcmp(mem, surfaces.data(), n * sizeof(ANARISurface)) == 0);
  std::vector<ANARISurface> replacements;
  if (c.rewriteOthers) {
    for (size_t i = released; i < n; i++) {
      replacements.push_back(makeSurface(d, n + i, cells));
      mem[i] = replacements.back();
    }
  }
  anariUnmapArray(d, array);
  anariCommitParameters(d, group);
  anariCommitParameters(d, world);
  appendFrame(d, frame, images.second);

  anariRelease(d, frame);
  anariRelease(d, renderer);
  anariRelease(d, camera);
  anariRelease(d, world);
  anariRelease(d, instance);
  anariRelease(d, group);
  anariRelease(d, array);
  for (size_t i = released; i < n; i++)
    anariRelease(d, surfaces[i]);
  for (ANARISurface s : replacements)
    anariRelease(d, s);
  return images;
}

// How many pixels are not the background's colour (the corner's).
static size_t litPixels(const std::vector<uint8_t> &image)
{
  size_t lit = 0;
  for (size_t i = 0; i < image.size(); i += 4)
    lit += std::memcmp(&image[i], &image[0], 4) != 0;
  return lit;
}

// The pixels one surface covers: see makeSurface().
static double triangleArea(const Case &c)
{
  size_t side = 1;
  while (side * side < 2 * c.surfaces)
    side++;
  const double cell = 256. / double(side);
  return .5 * (.8 * cell) * (.8 * cell);
}

int main()
{
  const char *wrapped = std::getenv("ANARI_GUARD_WRAPPED_DEVICE");
  CHECK(wrapped);

  ANARILibrary directLibrary = anariLoadLibrary(wrapped, onStatus, nullptr);
  CHECK(directLibrary);
  ANARIDevice direct = anariNewDevice(directLibrary, "default");
  CHECK(direct);

  ANARILibrary guardLibrary = anariLoadLibrary("guard", onStatus, nullptr);
  CHECK(guardLibrary);
  ANARIDevice guarded = anariNewDevice(guardLibrary, "default");
  CHECK(guarded);

  // 2500 handles are 20 KB: inline at the default array ceiling, in a region
  // when the test is run with a ceiling below that.
  const Case cases[] = {
      {"shared, nothing changed", false, false, 2},
      {"shared, the other element rewritten", false, true, 2},
      {"managed, nothing changed", true, false, 2},
      {"managed, the other element rewritten", true, true, 2},
      {"shared and large, nothing changed", false, false, 2500},
      {"shared and large, the others rewritten", false, true, 2500},
      {"managed and large, the others rewritten", true, true, 2500},
  };

  for (const Case &c : cases) {
    std::printf("%s\n", c.name);
    std::fflush(stdout);
    const Images expected = render(direct, c);
    // The picture shows every surface, so one that went missing would show.
    CHECK(litPixels(expected.first) > .9 * triangleArea(c) * c.surfaces);
    CHECK(litPixels(expected.second) > .9 * triangleArea(c) * c.surfaces);
    // In-process the released surfaces are still drawn.
    if (c.rewriteOthers)
      CHECK(expected.second != expected.first);
    else
      CHECK(expected.second == expected.first);
    const Images actual = render(guarded, c);
    CHECK(actual.first == expected.first);
    CHECK(actual.second == expected.second);
  }
  CHECK(errors == 0);

  anariRelease(guarded, guarded);
  anariUnloadLibrary(guardLibrary);
  anariRelease(direct, direct);
  // Left loaded: unloading a device's library can take its threads' code
  // from under them (helide's TBB workers may still be starting).
  (void)directLibrary;
  std::printf("passed\n");
  return 0;
}
