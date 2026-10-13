// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
#include <anari/anari.h>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string_view>

static int errors = 0;
static void check(bool ok, const char *message)
{
  std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", message);
  errors += !ok;
}

int main(int argc, char **argv)
{
  auto lib = anariLoadLibrary("hydra", nullptr, nullptr);
  auto d = lib ? anariNewDevice(lib, argc > 1 ? argv[1] : "storm") : nullptr;
  if (!d)
    return 2;
  auto group = anariNewGroup(d);
  auto world = anariNewWorld(d);
  anariCommitParameters(d, group);
  anariCommitParameters(d, world);
  for (ANARIObject object : {ANARIObject(group), ANARIObject(world)}) {
    float b[6] = {};
    check(
        anariGetProperty(
            d, object, "bounds", ANARI_FLOAT32_BOX3, b, sizeof(b), ANARI_WAIT),
        "empty bounds query succeeds");
    check(b[0] > b[3] && b[1] > b[4] && b[2] > b[5],
        "empty bounds have reversed endpoints");
  }
  float vertices[] = {-1, -2, 0, 2, -2, 0, 0, 3, 1, 100, 100, 100};
  auto positions =
      anariNewArray1D(d, vertices, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  auto geometry = anariNewGeometry(d, "triangle");
  anariSetParameter(d, geometry, "vertex.position", ANARI_ARRAY1D, &positions);
  anariCommitParameters(d, geometry);
  auto surface = anariNewSurface(d);
  anariSetParameter(d, surface, "geometry", ANARI_GEOMETRY, &geometry);
  anariCommitParameters(d, surface);
  auto surfaces =
      anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_SURFACE, 1);
  *static_cast<ANARISurface *>(anariMapArray(d, surfaces)) = surface;
  anariUnmapArray(d, surfaces);
  anariSetParameter(d, group, "surface", ANARI_ARRAY1D, &surfaces);
  anariSetParameter(d, world, "surface", ANARI_ARRAY1D, &surfaces);
  anariCommitParameters(d, group);
  anariCommitParameters(d, world);
  auto expect = [&](ANARIObject object,
                    const float *expected,
                    const char *label,
                    uint32_t flags = ANARI_WAIT) {
    float b[6] = {};
    bool ok = anariGetProperty(
        d, object, "bounds", ANARI_FLOAT32_BOX3, b, sizeof(b), flags);
    for (int i = 0; i < 6; ++i)
      ok = ok && std::fabs(b[i] - expected[i]) < 0.0001f;
    check(ok, label);
  };
  const float triangleBounds[] = {-1, -2, 0, 2, 3, 1};
  expect(group, triangleBounds, "group uses complete triangles only");
  expect(
      world, triangleBounds, "world direct surfaces included before rendering");
  auto instance = anariNewInstance(d, "transform");
  anariSetParameter(d, instance, "group", ANARI_GROUP, &group);
  // Rotate 90 degrees about Z, scale (2,3,4), translate (10,20,30).
  float transform[] = {0, 2, 0, 0, -3, 0, 0, 0, 0, 0, 4, 0, 10, 20, 30, 1};
  anariSetParameter(d, instance, "transform", ANARI_FLOAT32_MAT4, transform);
  anariCommitParameters(d, instance);
  auto instances =
      anariNewArray1D(d, &instance, nullptr, nullptr, ANARI_INSTANCE, 1);
  anariSetParameter(d, world, "instance", ANARI_ARRAY1D, &instances);
  anariCommitParameters(d, world);
  const float combined[] = {-1, -2, 0, 16, 24, 34};
  expect(world, combined, "world unions direct and rotated scaled instances");
  anariUnsetParameter(d, world, "surface");
  anariCommitParameters(d, world);
  const float transformed[] = {1, 18, 30, 16, 24, 34};
  expect(world, transformed, "direct surface removal updates bounds");
  auto *mapped = static_cast<float *>(anariMapArray(d, positions));
  mapped[0] = -4;
  anariUnmapArray(d, positions);
  expect(world,
      transformed,
      "NO_WAIT does not flush queued array edits",
      ANARI_NO_WAIT);
  const float edited[] = {1, 12, 30, 16, 24, 34};
  expect(world, edited, "array edits update instanced geometry bounds");
  expect(
      world, edited, "NO_WAIT returns completed query snapshot", ANARI_NO_WAIT);
  float untouched[6] = {42, 42, 42, 42, 42, 42};
  check(!anariGetProperty(d,
            world,
            "bounds",
            ANARI_FLOAT32,
            untouched,
            sizeof(untouched),
            ANARI_WAIT)
          && untouched[0] == 42,
      "wrong type rejected");
  check(!anariGetProperty(d,
            world,
            "bounds",
            ANARI_FLOAT32_BOX3,
            untouched,
            sizeof(float),
            ANARI_WAIT)
          && untouched[0] == 42,
      "short output rejected");
  check(!anariGetProperty(d,
            world,
            "unknown",
            ANARI_FLOAT32_BOX3,
            untouched,
            sizeof(untouched),
            ANARI_WAIT)
          && untouched[0] == 42,
      "unknown property rejected");
  // Radiused subtypes share this scene but have independent expected extents.
  anariUnsetParameter(d, world, "instance");
  anariSetParameter(d, world, "surface", ANARI_ARRAY1D, &surfaces);
  anariCommitParameters(d, world);
  for (const char *subtype : {"sphere", "curve", "cone", "cylinder"}) {
    float points[] = {0, 0, 0, 0, 0, 2};
    float radii[] = {0.5f, 0.25f};
    auto pa =
        anariNewArray1D(d, points, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
    auto ra = anariNewArray1D(d, radii, nullptr, nullptr, ANARI_FLOAT32, 2);
    auto g = anariNewGeometry(d, subtype);
    anariSetParameter(d, g, "vertex.position", ANARI_ARRAY1D, &pa);
    // Cylinders take primitive.radius (per segment), not vertex.radius (SPEC
    // KHR_GEOMETRY_CYLINDER); the single 2-vertex segment here only ever
    // reads radii[0], same as the r[0] edit below.
    const bool cylinderRadius = std::string_view(subtype) == "cylinder";
    anariSetParameter(d,
        g,
        cylinderRadius ? "primitive.radius" : "vertex.radius",
        ANARI_ARRAY1D,
        &ra);
    anariCommitParameters(d, g);
    anariSetParameter(d, surface, "geometry", ANARI_GEOMETRY, &g);
    anariCommitParameters(d, surface);
    const bool tube = std::string_view(subtype) == "cone"
        || std::string_view(subtype) == "cylinder";
    const float initial[] = {
        -0.5f, -0.5f, tube ? 0.f : -0.5f, 0.5f, 0.5f, tube ? 2.f : 2.25f};
    expect(world, initial, subtype);
    auto *r = static_cast<float *>(anariMapArray(d, ra));
    r[0] = 1.f;
    anariUnmapArray(d, ra);
    const float resized[] = {
        -1, -1, tube ? 0.f : -1.f, 1, 1, tube ? 2.f : 2.25f};
    expect(world, resized, "radius array edit updates bounds");
    auto *p = static_cast<float *>(anariMapArray(d, pa));
    p[5] = 4.f;
    anariUnmapArray(d, pa);
    const float moved[] = {-1, -1, tube ? 0.f : -1.f, 1, 1, tube ? 4.f : 4.25f};
    expect(world, moved, "position array edit updates radiused bounds");
    anariUnsetParameter(
        d, g, cylinderRadius ? "primitive.radius" : "vertex.radius");
    float radius = 2.f;
    anariSetParameter(d, g, "radius", ANARI_FLOAT32, &radius);
    anariCommitParameters(d, g);
    const float global[] = {-2, -2, tube ? 0.f : -2.f, 2, 2, tube ? 4.f : 6.f};
    expect(world, global, "geometry recommit updates global radius bounds");
    anariRelease(d, g);
    anariRelease(d, pa);
    anariRelease(d, ra);
  }
  // Explicit triangle indices exclude unused vertices; index unmaps are live.
  uint32_t face[] = {0, 1, 2};
  auto indices =
      anariNewArray1D(d, face, nullptr, nullptr, ANARI_UINT32_VEC3, 1);
  anariSetParameter(d, geometry, "primitive.index", ANARI_ARRAY1D, &indices);
  anariCommitParameters(d, geometry);
  anariSetParameter(d, surface, "geometry", ANARI_GEOMETRY, &geometry);
  anariCommitParameters(d, surface);
  const float indexed[] = {-4, -2, 0, 2, 3, 1};
  expect(world, indexed, "indexed triangles ignore unused vertices");
  auto *faceMap = static_cast<uint32_t *>(anariMapArray(d, indices));
  faceMap[2] = 3;
  anariUnmapArray(d, indices);
  const float changedIndex[] = {-4, -2, 0, 100, 100, 100};
  expect(world, changedIndex, "index array edit changes bounds");
  anariRelease(d, indices);

  auto quad = anariNewGeometry(d, "quad");
  anariSetParameter(d, quad, "vertex.position", ANARI_ARRAY1D, &positions);
  anariCommitParameters(d, quad);
  anariSetParameter(d, surface, "geometry", ANARI_GEOMETRY, &quad);
  anariCommitParameters(d, surface);
  expect(world, changedIndex, "quad bounds include all four vertices");
  anariRelease(d, quad);

  // A waiting query queues behind an in-flight render, without deadlocking.
  auto camera = anariNewCamera(d, "perspective");
  anariCommitParameters(d, camera);
  auto renderer = anariNewRenderer(d, "default");
  anariCommitParameters(d, renderer);
  auto frame = anariNewFrame(d);
  uint32_t size[] = {16, 16};
  ANARIDataType format = ANARI_UFIXED8_RGBA_SRGB;
  anariSetParameter(d, frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(d, frame, "channel.color", ANARI_DATA_TYPE, &format);
  anariSetParameter(d, frame, "world", ANARI_WORLD, &world);
  anariSetParameter(d, frame, "camera", ANARI_CAMERA, &camera);
  anariSetParameter(d, frame, "renderer", ANARI_RENDERER, &renderer);
  anariCommitParameters(d, frame);
  anariRenderFrame(d, frame);
  expect(
      world, changedIndex, "WAIT bounds safely observes an in-flight render");
  anariFrameReady(d, frame, ANARI_WAIT);
  anariRelease(d, frame);
  anariRelease(d, renderer);
  anariRelease(d, camera);

  auto *surfaceMap = static_cast<ANARISurface *>(anariMapArray(d, surfaces));
  surfaceMap[0] = nullptr;
  anariUnmapArray(d, surfaces);
  float removed[6];
  check(anariGetProperty(d,
            group,
            "bounds",
            ANARI_FLOAT32_BOX3,
            removed,
            sizeof(removed),
            ANARI_WAIT)
          && removed[0] > removed[3],
      "object array removal updates group bounds");
  check(!anariGetProperty(d,
            world,
            "bounds",
            ANARI_FLOAT32_BOX3,
            nullptr,
            sizeof(removed),
            ANARI_WAIT),
      "null output rejected");
  anariUnsetParameter(d, group, "surface");
  anariUnsetParameter(d, world, "surface");
  anariCommitParameters(d, group);
  anariCommitParameters(d, world);
  float empty[6];
  check(anariGetProperty(d,
            world,
            "bounds",
            ANARI_FLOAT32_BOX3,
            empty,
            sizeof(empty),
            ANARI_WAIT)
          && empty[0] > empty[3],
      "all removals restore empty bounds");
  for (ANARIObject object : {ANARIObject(positions),
           ANARIObject(geometry),
           ANARIObject(surface),
           ANARIObject(surfaces),
           ANARIObject(instance),
           ANARIObject(instances)})
    anariRelease(d, object);
  anariRelease(d, group);
  anariRelease(d, world);
  anariRelease(d, d);
  anariUnloadLibrary(lib);
  return errors ? 1 : 0;
}
