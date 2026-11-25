// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Bulk data: arrays of every kind on both sides of the array ceiling, made
// once and written again, and mapped frames on both sides of their own
// threshold, give the same pixels through the guard device as the wrapped
// device does in-process; and regions come and go with the objects they
// belong to. Run at the default ceiling of 1 MB, where every array of the
// scenes is below it, and at one of 4 KB, where the larger ones are above.

#include "TestSupport.h"

#include <anari/anari.h>

#include <cstdlib>
#include <cstring>
#include <vector>

#include <unistd.h>

using namespace guard_test;

static void onStatus(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_WARNING)
    std::fprintf(stderr, "[status] %s\n", message);
}

// How the application hands over an array.
enum class Path
{
  SHARED,
  MANAGED,
  PARAMETER // anariMapParameterArray
};

struct Scene
{
  const char *name;
  size_t surfaces;
  size_t trianglesPerSurface;
  Path path;
  uint32_t width;
  uint32_t height;
  // How many times each array the application hands over is written: once
  // when it is made, and then again.
  int writes;
  // How many regions that takes at the default ceiling.
  int regions;
};

struct Images
{
  // One for each time the arrays were written
  std::vector<std::vector<uint8_t>> frames;
  uint64_t regions{0}; // made on the way, by the guard device's own count
};

// Zero for a device that is not the guard device.
static uint64_t regionsCreated(ANARIDevice d)
{
  uint64_t value = 0;
  anariGetProperty(d,
      d,
      "guard.regionsCreated",
      ANARI_UINT64,
      &value,
      sizeof(value),
      ANARI_NO_WAIT);
  return value;
}

static uint32_t lcgState = 1;
static float uniform()
{
  lcgState = lcgState * 1664525u + 1013904223u;
  return float(lcgState >> 8) / float(1u << 24);
}

// Three vertices of a small triangle somewhere in front of the camera.
static void makeTriangle(float *v)
{
  const float cx = uniform() * 2.f - 1.f;
  const float cy = uniform() * 2.f - 1.f;
  const float cz = -3.f - uniform() * 2.f;
  for (int i = 0; i < 3; i++) {
    v[3 * i + 0] = cx + (uniform() - .5f) * .3f;
    v[3 * i + 1] = cy + (uniform() - .5f) * .3f;
    v[3 * i + 2] = cz;
  }
}

static void appendFrame(
    ANARIDevice d, ANARIFrame frame, std::vector<uint8_t> &out)
{
  anariRenderFrame(d, frame);
  CHECK(anariFrameReady(d, frame, ANARI_WAIT) == 1);
  for (const char *channel : {"channel.color", "channel.depth"}) {
    uint32_t w = 0, h = 0;
    ANARIDataType type = ANARI_UNKNOWN;
    auto *p = static_cast<const uint8_t *>(
        anariMapFrame(d, frame, channel, &w, &h, &type));
    CHECK(p && w > 0 && h > 0);
    out.insert(out.end(), p, p + size_t(w) * h * 4);
    anariUnmapFrame(d, frame, channel);
  }
}

static Images render(ANARIDevice d, const Scene &scene)
{
  lcgState = 1;
  const uint64_t regionsBefore = regionsCreated(d);
  const size_t vertices = scene.trianglesPerSurface * 3;

  // Kept until the end, as shared arrays require.
  std::vector<std::vector<float>> positions(scene.surfaces);
  std::vector<ANARIGeometry> geometries(scene.surfaces);
  std::vector<ANARIArray1D> positionArrays(scene.surfaces, nullptr);
  std::vector<ANARISurface> surfaces(scene.surfaces);

  ANARIMaterial material = anariNewMaterial(d, "matte");
  anariSetParameter(d, material, "color", ANARI_STRING, "color");
  anariCommitParameters(d, material);

  for (size_t s = 0; s < scene.surfaces; s++) {
    auto &p = positions[s];
    p.resize(vertices * 3);
    for (size_t t = 0; t < scene.trianglesPerSurface; t++)
      makeTriangle(&p[t * 9]);

    ANARIGeometry g = geometries[s] = anariNewGeometry(d, "triangle");

    if (scene.path == Path::SHARED) {
      positionArrays[s] = anariNewArray1D(
          d, p.data(), nullptr, nullptr, ANARI_FLOAT32_VEC3, vertices);
    } else if (scene.path == Path::MANAGED) {
      positionArrays[s] = anariNewArray1D(
          d, nullptr, nullptr, nullptr, ANARI_FLOAT32_VEC3, vertices);
      void *mem = anariMapArray(d, positionArrays[s]);
      CHECK(mem);
      std::memcpy(mem, p.data(), p.size() * sizeof(float));
      anariUnmapArray(d, positionArrays[s]);
    } else {
      uint64_t stride = 0;
      void *mem = anariMapParameterArray1D(
          d, g, "vertex.position", ANARI_FLOAT32_VEC3, vertices, &stride);
      CHECK(mem && stride == 3 * sizeof(float));
      std::memcpy(mem, p.data(), p.size() * sizeof(float));
      anariUnmapParameterArray(d, g, "vertex.position");
    }
    if (positionArrays[s]) {
      anariSetParameter(
          d, g, "vertex.position", ANARI_ARRAY1D, &positionArrays[s]);
    }

    // Colors always through a managed array.
    ANARIArray1D colors = anariNewArray1D(
        d, nullptr, nullptr, nullptr, ANARI_FLOAT32_VEC4, vertices);
    auto *c = static_cast<float *>(anariMapArray(d, colors));
    CHECK(c);
    for (size_t i = 0; i < vertices; i++) {
      c[4 * i + 0] = uniform();
      c[4 * i + 1] = uniform();
      c[4 * i + 2] = uniform();
      c[4 * i + 3] = 1.f;
    }
    anariUnmapArray(d, colors);
    anariSetParameter(d, g, "vertex.color", ANARI_ARRAY1D, &colors);
    anariRelease(d, colors);
    anariCommitParameters(d, g);

    surfaces[s] = anariNewSurface(d);
    anariSetParameter(d, surfaces[s], "geometry", ANARI_GEOMETRY, &g);
    anariSetParameter(d, surfaces[s], "material", ANARI_MATERIAL, &material);
    anariCommitParameters(d, surfaces[s]);
  }

  // The world's surfaces: an array of object handles.
  ANARIWorld world = anariNewWorld(d);
  ANARIArray1D surfaceArray = nullptr;
  std::vector<ANARISurface> surfaceMemory = surfaces; // the shared array's
  if (scene.path == Path::SHARED) {
    surfaceArray = anariNewArray1D(d,
        surfaceMemory.data(),
        nullptr,
        nullptr,
        ANARI_SURFACE,
        scene.surfaces);
  } else if (scene.path == Path::MANAGED) {
    surfaceArray = anariNewArray1D(
        d, nullptr, nullptr, nullptr, ANARI_SURFACE, scene.surfaces);
    void *mem = anariMapArray(d, surfaceArray);
    CHECK(mem);
    std::memcpy(mem, surfaces.data(), surfaces.size() * sizeof(ANARISurface));
    anariUnmapArray(d, surfaceArray);
  } else {
    uint64_t stride = 0;
    void *mem = anariMapParameterArray1D(
        d, world, "surface", ANARI_SURFACE, scene.surfaces, &stride);
    CHECK(mem && stride == sizeof(ANARISurface));
    std::memcpy(mem, surfaces.data(), surfaces.size() * sizeof(ANARISurface));
    anariUnmapParameterArray(d, world, "surface");
  }
  if (surfaceArray)
    anariSetParameter(d, world, "surface", ANARI_ARRAY1D, &surfaceArray);
  anariCommitParameters(d, world);

  ANARICamera camera = anariNewCamera(d, "perspective");
  const float aspect = float(scene.width) / float(scene.height);
  anariSetParameter(d, camera, "aspect", ANARI_FLOAT32, &aspect);
  // Stated, not left to the defaults: helide sees nothing without them.
  const float position[3] = {0.f, 0.f, 0.f};
  const float direction[3] = {0.f, 0.f, -1.f};
  const float up[3] = {0.f, 1.f, 0.f};
  anariSetParameter(d, camera, "position", ANARI_FLOAT32_VEC3, position);
  anariSetParameter(d, camera, "direction", ANARI_FLOAT32_VEC3, direction);
  anariSetParameter(d, camera, "up", ANARI_FLOAT32_VEC3, up);
  anariCommitParameters(d, camera);

  ANARIRenderer renderer = anariNewRenderer(d, "default");
  const float ambient = 1.f;
  anariSetParameter(d, renderer, "ambientRadiance", ANARI_FLOAT32, &ambient);
  anariCommitParameters(d, renderer);

  ANARIFrame frame = anariNewFrame(d);
  const uint32_t size[2] = {scene.width, scene.height};
  const ANARIDataType colorType = ANARI_UFIXED8_RGBA_SRGB;
  const ANARIDataType depthType = ANARI_FLOAT32;
  anariSetParameter(d, frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(d, frame, "channel.color", ANARI_DATA_TYPE, &colorType);
  anariSetParameter(d, frame, "channel.depth", ANARI_DATA_TYPE, &depthType);
  anariSetParameter(d, frame, "camera", ANARI_CAMERA, &camera);
  anariSetParameter(d, frame, "renderer", ANARI_RENDERER, &renderer);
  anariSetParameter(d, frame, "world", ANARI_WORLD, &world);
  anariCommitParameters(d, frame);

  Images images;
  images.frames.emplace_back();
  appendFrame(d, frame, images.frames.back());

  // Change the contents of every array the application handed over: move the
  // triangles, and at each second writing drop the second half of the
  // surfaces, which the next puts back.
  uint64_t stride = 0;
  for (int write = 2; write <= scene.writes; write++) {
    for (size_t s = 0; s < scene.surfaces; s++) {
      auto &p = positions[s];
      float *mem = nullptr;
      if (scene.path == Path::PARAMETER) {
        mem = static_cast<float *>(anariMapParameterArray1D(d,
            geometries[s],
            "vertex.position",
            ANARI_FLOAT32_VEC3,
            vertices,
            &stride));
        CHECK(mem);
        std::memcpy(mem, p.data(), p.size() * sizeof(float));
      } else {
        mem = static_cast<float *>(anariMapArray(d, positionArrays[s]));
        CHECK(mem);
        if (scene.path == Path::SHARED)
          CHECK(mem == p.data());
        else // a managed array still holds what was written to it
          CHECK(std::memcmp(mem, p.data(), p.size() * sizeof(float)) == 0);
      }
      for (size_t i = 0; i < vertices; i++)
        mem[3 * i] += .25f;
      if (mem != p.data())
        std::memcpy(p.data(), mem, p.size() * sizeof(float));
      if (scene.path == Path::PARAMETER)
        anariUnmapParameterArray(d, geometries[s], "vertex.position");
      else
        anariUnmapArray(d, positionArrays[s]);
      anariCommitParameters(d, geometries[s]);
    }
    if (scene.surfaces > 1) {
      const size_t half = scene.surfaces / 2;
      const bool drop = write % 2 == 0;
      ANARISurface *mem = nullptr;
      if (scene.path == Path::PARAMETER) {
        mem = static_cast<ANARISurface *>(anariMapParameterArray1D(
            d, world, "surface", ANARI_SURFACE, scene.surfaces, &stride));
        CHECK(mem);
      } else {
        mem = static_cast<ANARISurface *>(anariMapArray(d, surfaceArray));
        CHECK(mem);
        CHECK(std::memcmp(mem, surfaces.data(), half * sizeof(ANARISurface))
            == 0);
      }
      for (size_t s = 0; s < scene.surfaces; s++)
        mem[s] = surfaces[drop && s >= half ? s - half : s];
      if (scene.path == Path::PARAMETER)
        anariUnmapParameterArray(d, world, "surface");
      else
        anariUnmapArray(d, surfaceArray);
      anariCommitParameters(d, world);
    }
    images.frames.emplace_back();
    appendFrame(d, frame, images.frames.back());
  }
  images.regions = regionsCreated(d) - regionsBefore;

  anariRelease(d, frame);
  anariRelease(d, renderer);
  anariRelease(d, camera);
  anariRelease(d, world);
  if (surfaceArray)
    anariRelease(d, surfaceArray);
  for (size_t s = 0; s < scene.surfaces; s++) {
    anariRelease(d, surfaces[s]);
    anariRelease(d, geometries[s]);
    if (positionArrays[s])
      anariRelease(d, positionArrays[s]);
  }
  anariRelease(d, material);
  return images;
}

// Wait until the host has run every call made so far.
static void sync(ANARIDevice d)
{
  int32_t version = 0;
  anariGetProperty(
      d, d, "version", ANARI_INT32, &version, sizeof(version), ANARI_WAIT);
}

static int deleterRuns = 0;
static void onDelete(const void *, const void *)
{
  deleterRuns++;
}

static int smallDeleterRuns = 0;
static void onDeleteSmall(const void *, const void *)
{
  smallDeleterRuns++;
}

// Regions appear with the objects that need them and go when those do, in
// the application and in the host.
static void checkRegionLifetimes(
    ANARIDevice d, uint64_t ceiling, bool dropsPromptly)
{
  const pid_t self = getpid();
  const pid_t host = findChild(self, "anariGuardHost");
  CHECK(host > 0);
  sync(d);
  CHECK(countRegions(self) == 0);
  CHECK(countRegions(host) == 0);

  const uint64_t big = ceiling; // floats: four times the ceiling
  const uint64_t small = 1 << 8; // floats: 1 KB
  const uint64_t edge = ceiling / sizeof(float); // exactly the ceiling

  // Arrays at or above the ceiling: a region each, from the start
  ANARIArray1D managed =
      anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_FLOAT32, big);
  CHECK(countRegions(self) == 1);
  auto *mem = static_cast<float *>(anariMapArray(d, managed));
  CHECK(mem);
  for (uint64_t i = 0; i < big; i++)
    mem[i] = float(i);
  anariUnmapArray(d, managed);
  CHECK(anariMapArray(d, managed) == mem);
  CHECK(mem[big - 1] == float(big - 1));
  anariUnmapArray(d, managed);

  ANARIArray1D below =
      anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_FLOAT32, edge - 1);
  CHECK(countRegions(self) == 1);
  ANARIArray1D at =
      anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_FLOAT32, edge);
  CHECK(countRegions(self) == 2);

  std::vector<float> appMemory(big, 1.f);
  ANARIArray1D shared = anariNewArray1D(
      d, appMemory.data(), onDelete, nullptr, ANARI_FLOAT32, big);
  CHECK(countRegions(self) == 3);
  CHECK(anariMapArray(d, shared) == appMemory.data());
  anariUnmapArray(d, shared);
  CHECK(countRegions(self) == 3);

  sync(d);
  CHECK(countRegions(host) == 3);

  // A managed array below the ceiling: no region while it is filled, one
  // from its second mapping on, with what it held.
  ANARIArray1D smallManaged =
      anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_FLOAT32, small);
  auto *fill = static_cast<float *>(anariMapArray(d, smallManaged));
  CHECK(fill);
  for (uint64_t i = 0; i < small; i++)
    fill[i] = float(i);
  anariUnmapArray(d, smallManaged);
  CHECK(countRegions(self) == 3);
  auto *again = static_cast<float *>(anariMapArray(d, smallManaged));
  CHECK(again);
  CHECK(countRegions(self) == 4);
  for (uint64_t i = 0; i < small; i++)
    CHECK(again[i] == float(i));
  again[0] = -1.f;
  anariUnmapArray(d, smallManaged);
  sync(d);
  CHECK(countRegions(host) == 4);
  CHECK(anariMapArray(d, smallManaged) == again);
  CHECK(again[0] == -1.f);
  anariUnmapArray(d, smallManaged);
  CHECK(countRegions(self) == 4);

  // A shared array below the ceiling: no region when it is made, one from
  // its first unmap on.
  std::vector<float> smallMemory(small, 2.f);
  ANARIArray1D smallShared = anariNewArray1D(
      d, smallMemory.data(), onDeleteSmall, nullptr, ANARI_FLOAT32, small);
  CHECK(countRegions(self) == 4);
  for (int write = 2; write <= 3; write++) {
    CHECK(anariMapArray(d, smallShared) == smallMemory.data());
    CHECK(countRegions(self) == (write == 2 ? 4 : 5));
    smallMemory[0] = float(write);
    anariUnmapArray(d, smallShared);
    CHECK(countRegions(self) == 5);
  }
  sync(d);
  CHECK(countRegions(host) == 5);

  // One that is filled and never written again has none.
  ANARIArray1D once =
      anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_FLOAT32, small);
  CHECK(anariMapArray(d, once));
  anariUnmapArray(d, once);
  CHECK(countRegions(self) == 5);

  anariRelease(d, managed);
  anariRelease(d, smallManaged);
  anariRelease(d, once);
  anariRelease(d, below);
  anariRelease(d, at);
  CHECK(deleterRuns == 0);
  anariRelease(d, shared);
  CHECK(deleterRuns == 1);
  CHECK(smallDeleterRuns == 0);
  anariRelease(d, smallShared);
  CHECK(smallDeleterRuns == 1);
  CHECK(countRegions(self) == 0);
  sync(d);
  CHECK(countRegions(host) == 0);

  // Frames: one region per mapped channel, kept between mappings, replaced
  // when the size changes, gone with the frame.
  ANARICamera camera = anariNewCamera(d, "perspective");
  anariCommitParameters(d, camera);
  ANARIRenderer renderer = anariNewRenderer(d, "default");
  anariCommitParameters(d, renderer);
  ANARIWorld world = anariNewWorld(d);
  anariCommitParameters(d, world);
  ANARIFrame frame = anariNewFrame(d);
  uint32_t size[2] = {640, 480};
  const ANARIDataType colorType = ANARI_UFIXED8_RGBA_SRGB;
  anariSetParameter(d, frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(d, frame, "channel.color", ANARI_DATA_TYPE, &colorType);
  anariSetParameter(d, frame, "camera", ANARI_CAMERA, &camera);
  anariSetParameter(d, frame, "renderer", ANARI_RENDERER, &renderer);
  anariSetParameter(d, frame, "world", ANARI_WORLD, &world);
  anariCommitParameters(d, frame);

  auto mapColor = [&](uint32_t expectWidth) {
    anariRenderFrame(d, frame);
    CHECK(anariFrameReady(d, frame, ANARI_WAIT) == 1);
    uint32_t w = 0, h = 0;
    ANARIDataType type = ANARI_UNKNOWN;
    const void *p = anariMapFrame(d, frame, "channel.color", &w, &h, &type);
    CHECK(p && w == expectWidth && type == colorType);
    return p;
  };
  auto resize = [&](uint32_t width, uint32_t height) {
    size[0] = width;
    size[1] = height;
    anariSetParameter(d, frame, "size", ANARI_UINT32_VEC2, size);
    anariCommitParameters(d, frame);
  };

  const void *pixels = mapColor(640);
  CHECK(countRegions(self) == 1);
  CHECK(countRegions(host) == 1);
  anariUnmapFrame(d, frame, "channel.color");
  CHECK(mapColor(640) == pixels); // rendered again, nothing changed
  anariUnmapFrame(d, frame, "channel.color");
  CHECK(countRegions(self) == 1);

  resize(800, 600);
  mapColor(800);
  anariUnmapFrame(d, frame, "channel.color");
  CHECK(countRegions(self) == 1);
  CHECK(countRegions(host) == 1);

  // Below 16 KB a frame needs no region; the old one goes. The array ceiling
  // has no say in that: 48 by 48 is 9 KB, more than the lower ceiling this
  // test is run with.
  resize(48, 48);
  mapColor(48);
  anariUnmapFrame(d, frame, "channel.color");
  CHECK(countRegions(self) == 0);
  CHECK(countRegions(host) == 0);

  // Nor in this: 64 by 64 is exactly 16 KB, far below the default ceiling.
  resize(64, 64);
  mapColor(64);
  anariUnmapFrame(d, frame, "channel.color");
  CHECK(countRegions(self) == 1);
  CHECK(countRegions(host) == 1);

  resize(640, 480);
  mapColor(640);
  anariUnmapFrame(d, frame, "channel.color");
  CHECK(countRegions(self) == 1);

  anariRelease(d, frame);
  anariRelease(d, world);
  anariRelease(d, renderer);
  anariRelease(d, camera);
  CHECK(countRegions(self) == 0);
  sync(d);
  CHECK(countRegions(host) == 0);

  // A parameter array mapping at or above the ceiling: the application's
  // region goes at the unmap, the host's when the wrapped device drops the
  // array. Below the ceiling there is never one.
  ANARIGeometry geometry = anariNewGeometry(d, "triangle");
  uint64_t stride = 0;
  void *mapped = anariMapParameterArray1D(
      d, geometry, "vertex.color", ANARI_FLOAT32_VEC4, small / 4, &stride);
  CHECK(mapped && stride == 16);
  CHECK(countRegions(self) == 0);
  std::memset(mapped, 0, small * 4);
  anariUnmapParameterArray(d, geometry, "vertex.color");
  sync(d);
  CHECK(countRegions(host) == 0);

  mapped = anariMapParameterArray1D(
      d, geometry, "vertex.position", ANARI_FLOAT32_VEC3, big / 4, &stride);
  CHECK(mapped && stride == 12);
  CHECK(countRegions(self) == 1);
  std::memset(mapped, 0, big * 3);
  anariUnmapParameterArray(d, geometry, "vertex.position");
  CHECK(countRegions(self) == 0);
  sync(d);
  CHECK(countRegions(host) == 1);
  anariRelease(d, geometry);
  sync(d);
  // When the wrapped device drops the array is its own business: helide does
  // with the geometry; another may keep it until the device goes.
  if (dropsPromptly)
    CHECK(countRegions(host) == 0);
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

  const char *ceilingText = std::getenv("ANARI_GUARD_INLINE_THRESHOLD");
  const uint64_t defaultCeiling = 1024 * 1024;
  const uint64_t ceiling =
      ceilingText ? std::strtoull(ceilingText, nullptr, 10) : defaultCeiling;
  CHECK(ceiling >= 4096);

  // 4000 triangles are 144 KB of positions; 2500 surfaces are 20 KB of
  // handles; 640x480 is 1.2 MB a channel and 32x32 is 4 KB. One surface of
  // 20 triangles is 720 bytes of positions.
  //
  // The regions, at the default ceiling: one for each position array that is
  // written again, unless it goes by a parameter mapping; none for the
  // surfaces or the colors; two for the frame's channels at 640x480.
  const Scene scenes[] = {
      {"one surface, shared", 1, 4000, Path::SHARED, 640, 480, 3, 1 + 2},
      {"one surface, managed", 1, 4000, Path::MANAGED, 640, 480, 3, 1 + 2},
      {"one surface, parameter mapping",
          1,
          4000,
          Path::PARAMETER,
          640,
          480,
          3,
          2},
      {"one surface, shared, made once", 1, 4000, Path::SHARED, 32, 32, 1, 0},
      {"one surface, managed, made once", 1, 4000, Path::MANAGED, 32, 32, 1, 0},
      {"one surface, parameter mapping, made once",
          1,
          4000,
          Path::PARAMETER,
          32,
          32,
          1,
          0},
      {"many surfaces, shared", 2500, 1, Path::SHARED, 640, 480, 2, 2500 + 2},
      {"many surfaces, managed", 2500, 1, Path::MANAGED, 640, 480, 3, 2500 + 2},
      {"many surfaces, parameter mapping",
          2500,
          1,
          Path::PARAMETER,
          640,
          480,
          3,
          2},
      {"small everything, shared", 3, 20, Path::SHARED, 32, 32, 3, 3},
      {"small everything, managed", 3, 20, Path::MANAGED, 32, 32, 3, 3},
      {"small everything, parameter mapping",
          3,
          20,
          Path::PARAMETER,
          32,
          32,
          3,
          0},
      {"small everything, shared, made once",
          3,
          20,
          Path::SHARED,
          32,
          32,
          1,
          0},
      {"small everything, managed, made once",
          3,
          20,
          Path::MANAGED,
          32,
          32,
          1,
          0},
  };

  for (const Scene &scene : scenes) {
    std::printf("%s\n", scene.name);
    std::fflush(stdout);
    const Images expected = render(direct, scene);
    const Images actual = render(guarded, scene);
    CHECK(int(expected.frames.size()) == scene.writes);
    CHECK(actual.frames.size() == expected.frames.size());
    for (size_t i = 0; i < expected.frames.size(); i++) {
      // Each change shows
      if (i > 0)
        CHECK(expected.frames[i] != expected.frames[i - 1]);
      CHECK(actual.frames[i].size() == expected.frames[i].size());
      CHECK(actual.frames[i] == expected.frames[i]);
    }
    if (ceiling == defaultCeiling)
      CHECK(actual.regions == uint64_t(scene.regions));
  }

  checkRegionLifetimes(guarded, ceiling, std::string(wrapped) == "helide");

  anariRelease(guarded, guarded);
  anariUnloadLibrary(guardLibrary);
  anariRelease(direct, direct);
  // Left loaded: unloading a device's library can take its threads' code
  // from under them (helide's TBB workers may still be starting).
  (void)directLibrary;
  std::printf("passed\n");
  return 0;
}
