// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Renders an instanced scene, then creates many more objects of every type
// with an ISPC mirror and renders again without recommitting the scene. The
// second image must match the first: creating objects must never move the
// ISPC structs that committed objects already point to.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <cstdint>
#include <cstdio>
#include <vector>
#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace {

using namespace anari::math;

constexpr uint2 IMAGE_SIZE = {16, 16};

// Enough objects of each type to grow a contiguous backing store many times
constexpr int NUM_EXTRA_OBJECTS = 2000;

// An instance (id 5) of a group holding one surface (id 3): a matte triangle
// pair covering the lower left half of the camera's [-1, 1]^2 view
anari::World makeWorld(anari::Device d)
{
  const std::vector<float3> verts = {
      {-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {-1.f, 1.f, 0.f}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(
      d, geom, "vertex.position", verts.data(), verts.size());
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", float3(0.8f, 0.4f, 0.2f));
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::setParameter(d, surface, "id", uint32_t(3));
  anari::commitParameters(d, surface);

  auto group = anari::newObject<anari::Group>(d);
  anari::setParameterArray1D(d, group, "surface", &surface, 1);
  anari::commitParameters(d, group);
  anari::release(d, surface);

  auto inst = anari::newObject<anari::Instance>(d, "transform");
  anari::setAndReleaseParameter(d, inst, "group", group);
  anari::setParameter(d, inst, "id", uint32_t(5));
  anari::commitParameters(d, inst);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "instance", &inst, 1);
  anari::commitParameters(d, world);
  anari::release(d, inst);
  return world;
}

anari::Frame makeFrame(
    anari::Device d, anari::World world, anari::Renderer renderer)
{
  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 1.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", 2.f);
  anari::commitParameters(d, camera);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "channel.objectId", ANARI_UINT32);
  anari::setParameter(d, frame, "channel.instanceId", ANARI_UINT32);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);
  return frame;
}

// Recommits 'renderer' with a new background, which also makes the next
// render of a frame using it not be skipped as unchanged
void setBackground(anari::Device d, anari::Renderer renderer, float4 color)
{
  anari::setParameter(d, renderer, "background", color);
  anari::commitParameters(d, renderer);
}

struct Image
{
  std::vector<float4> color;
  std::vector<uint32_t> objectId;
  std::vector<uint32_t> instanceId;
};

template <typename T>
std::vector<T> readChannel(anari::Device d, anari::Frame f, const char *name)
{
  std::vector<T> pixels;
  auto fb = anari::map<T>(d, f, name);
  if (fb.data && fb.width == IMAGE_SIZE.x && fb.height == IMAGE_SIZE.y)
    pixels.assign(fb.data, fb.data + fb.width * fb.height);
  else
    std::printf("FAIL: could not map %s\n", name);
  anari::unmap(d, f, name);
  return pixels;
}

Image render(anari::Device d, anari::Frame frame)
{
  anari::render(d, frame);
  anari::wait(d, frame);
  return {readChannel<float4>(d, frame, "channel.color"),
      readChannel<uint32_t>(d, frame, "channel.objectId"),
      readChannel<uint32_t>(d, frame, "channel.instanceId")};
}

const float4 BACKGROUND_A = {0.f, 0.f, 1.f, 1.f};
const float4 BACKGROUND_B = {1.f, 0.f, 0.f, 1.f};

// The lower left pixel sees the instanced surface, the upper right misses
bool checkReference(const Image &img)
{
  if (img.color.empty() || img.objectId.empty() || img.instanceId.empty())
    return false;
  const size_t hit = 0;
  const size_t miss = img.color.size() - 1;
  const bool pass = img.objectId[hit] == 3 && img.instanceId[hit] == 5
      && img.objectId[miss] == ~0u && img.color[miss] == BACKGROUND_A
      && img.color[hit] != img.color[miss];
  std::printf("%s reference image\n", pass ? "PASS" : "FAIL");
  return pass;
}

// 'img' must match 'reference' except for missed pixels showing 'background'
bool sameScene(const Image &reference,
    const Image &img,
    float4 background,
    const char *test)
{
  bool pass = !img.color.empty() && img.objectId == reference.objectId
      && img.instanceId == reference.instanceId;
  for (size_t i = 0; pass && i < img.color.size(); i++) {
    const bool miss = reference.objectId[i] == ~0u;
    pass = img.color[i] == (miss ? background : reference.color[i]);
  }
  std::printf("%s %s\n", pass ? "PASS" : "FAIL", test);
  return pass;
}

// Creates NUM_EXTRA_OBJECTS of every object type mirrored in ISPC, returning
// them all
std::vector<anari::Object> createObjects(anari::Device d)
{
  std::vector<anari::Object> objects;
  for (int i = 0; i < NUM_EXTRA_OBJECTS; i++) {
    objects.push_back(anari::newObject<anari::Geometry>(d, "triangle"));
    objects.push_back(anari::newObject<anari::Material>(d, "matte"));
    objects.push_back(anari::newObject<anari::Surface>(d));
    objects.push_back(anari::newObject<anari::Group>(d));
    objects.push_back(anari::newObject<anari::Instance>(d, "transform"));
    objects.push_back(anari::newObject<anari::World>(d));
    objects.push_back(anari::newObject<anari::Camera>(d, "orthographic"));
    objects.push_back(anari::newObject<anari::Renderer>(d, "preview"));
    objects.push_back(anari::newObject<anari::Frame>(d));
  }
  return objects;
}

void release(anari::Device d, const std::vector<anari::Object> &objects)
{
  for (auto o : objects)
    anari::release(d, o);
}

// device errors (e.g. ISPC object size mismatches) fail the test
bool g_deviceError = false;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_ERROR)
    g_deviceError = true;
  if (severity <= ANARI_SEVERITY_WARNING)
    std::fprintf(stderr, "[ANARI] %s\n", message);
}

} // namespace

int main()
{
#ifdef __GLIBC__
  // Scribble over freed memory, so reading an ISPC struct from a slot's old
  // address renders garbage instead of stale-but-intact data. Elsewhere a
  // moved slot may go unnoticed; test_indexed_vector still catches it.
  mallopt(M_PERTURB, 0xa5);
#endif

  auto library = anari::loadLibrary("halcyon", statusFunc);
  if (!library) {
    std::fprintf(stderr, "failed to load halcyon library\n");
    return 1;
  }
  auto d = anari::newDevice(library, "default");

  auto world = makeWorld(d);
  auto renderer = anari::newObject<anari::Renderer>(d, "preview");
  setBackground(d, renderer, BACKGROUND_A);
  auto frame = makeFrame(d, world, renderer);

  const Image reference = render(d, frame);
  bool pass = checkReference(reference);

  auto objects = createObjects(d);
  setBackground(d, renderer, BACKGROUND_B);
  pass &= sameScene(reference,
      render(d, frame),
      BACKGROUND_B,
      "render after creating objects");

  // releasing them frees their slots for reuse without moving the others
  release(d, objects);
  objects = createObjects(d);
  setBackground(d, renderer, BACKGROUND_A);
  pass &= sameScene(reference,
      render(d, frame),
      BACKGROUND_A,
      "render after recycling object slots");
  release(d, objects);

  anari::release(d, frame);
  anari::release(d, renderer);
  anari::release(d, world);
  anari::release(d, d);
  anari::unloadLibrary(library);

  if (g_deviceError)
    std::printf("FAIL: device reported errors\n");

  return pass && !g_deviceError ? 0 : 1;
}
