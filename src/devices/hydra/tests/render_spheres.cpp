// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
// Compare rendered indexed spheres against ray/ellipsoid intersections,
// including a plane through their front hemispheres and edits to every copied
// input.
#include <anari/anari.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

static int errors = 0;
static void check(bool ok, const char *message)
{
  std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", message);
  errors += !ok;
}
static ANARIArray1D array(ANARIDevice d,
    const void *data,
    ANARIDataType type,
    size_t count,
    size_t bytes)
{
  auto a = anariNewArray1D(d, nullptr, nullptr, nullptr, type, count);
  std::memcpy(anariMapArray(d, a), data, bytes);
  anariUnmapArray(d, a);
  return a;
}
static void attach(
    ANARIDevice d, ANARIObject o, const char *name, ANARIArray1D a)
{
  anariSetParameter(d, o, name, ANARI_ARRAY1D, &a);
}
int main(int argc, char **argv)
{
  const bool storm = argc < 2 || !std::strcmp(argv[1], "storm");
  auto lib = anariLoadLibrary("hydra", nullptr, nullptr);
  auto d = lib ? anariNewDevice(lib, storm ? "storm" : "embree") : nullptr;
  if (!d)
    return 2;
  float centers[3][3] = {{-.4f, 0, 0}, {0, 1, 1}, {.4f, 0, -.2f}};
  float radii[] = {.6f, .7f, .5f};
  float colors[3][3] = {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}};
  uint64_t indices[] = {2, 0};
  uint32_t ids[] = {79, 31};
  auto pos = array(d, centers, ANARI_FLOAT32_VEC3, 3, sizeof(centers));
  auto rad = array(d, radii, ANARI_FLOAT32, 3, sizeof(radii));
  auto col = array(d, colors, ANARI_FLOAT32_VEC3, 3, sizeof(colors));
  auto idx = array(d, indices, ANARI_UINT64, 2, sizeof(indices));
  auto id = array(d, ids, ANARI_UINT32, 2, sizeof(ids));
  auto geometry = anariNewGeometry(d, "sphere");
  attach(d, geometry, "vertex.position", pos);
  attach(d, geometry, "vertex.radius", rad);
  attach(d, geometry, "vertex.attribute2", col);
  attach(d, geometry, "primitive.index", idx);
  attach(d, geometry, "primitive.id", id);
  anariCommitParameters(d, geometry);
  auto material = anariNewMaterial(d, "matte");
  anariSetParameter(d, material, "color", ANARI_STRING, "attribute2");
  anariCommitParameters(d, material);
  auto sphere = anariNewSurface(d);
  anariSetParameter(d, sphere, "geometry", ANARI_GEOMETRY, &geometry);
  anariSetParameter(d, sphere, "material", ANARI_MATERIAL, &material);
  uint32_t sphereId = 1;
  anariSetParameter(d, sphere, "id", ANARI_UINT32, &sphereId);
  anariCommitParameters(d, sphere);
  float planePoints[] = {-3, -3, .25f, 3, -3, .25f, 3, 3, .25f, -3, 3, .25f};
  auto planePos =
      array(d, planePoints, ANARI_FLOAT32_VEC3, 4, sizeof(planePoints));
  auto planeGeometry = anariNewGeometry(d, "quad");
  attach(d, planeGeometry, "vertex.position", planePos);
  anariCommitParameters(d, planeGeometry);
  auto plane = anariNewSurface(d);
  uint32_t planeId = 2;
  anariSetParameter(d, plane, "geometry", ANARI_GEOMETRY, &planeGeometry);
  anariSetParameter(d, plane, "id", ANARI_UINT32, &planeId);
  anariCommitParameters(d, plane);
  auto group = anariNewGroup(d);
  auto surfaces = array(d, &sphere, ANARI_SURFACE, 1, sizeof(sphere));
  attach(d, group, "surface", surfaces);
  anariCommitParameters(d, group);
  auto instance = anariNewInstance(d, "transform");
  anariSetParameter(d, instance, "group", ANARI_GROUP, &group);
  anariCommitParameters(d, instance);
  auto instances = array(d, &instance, ANARI_INSTANCE, 1, sizeof(instance));
  auto planeArray = array(d, &plane, ANARI_SURFACE, 1, sizeof(plane));
  auto world = anariNewWorld(d);
  attach(d, world, "instance", instances);
  anariCommitParameters(d, world);
  auto camera = anariNewCamera(d, "perspective");
  float eye[] = {0, 0, 4}, dir[] = {0, 0, -1}, fovy = .8f;
  anariSetParameter(d, camera, "position", ANARI_FLOAT32_VEC3, eye);
  anariSetParameter(d, camera, "direction", ANARI_FLOAT32_VEC3, dir);
  anariSetParameter(d, camera, "fovy", ANARI_FLOAT32, &fovy);
  anariCommitParameters(d, camera);
  auto renderer = anariNewRenderer(d, "default");
  anariCommitParameters(d, renderer);
  auto frame = anariNewFrame(d);
  uint32_t size[] = {192, 192};
  anariSetParameter(d, frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(d, frame, "camera", ANARI_CAMERA, &camera);
  anariSetParameter(d, frame, "renderer", ANARI_RENDERER, &renderer);
  anariSetParameter(d, frame, "world", ANARI_WORLD, &world);
  ANARIDataType depthType = ANARI_FLOAT32, idType = ANARI_UINT32;
  ANARIDataType colorType = ANARI_UFIXED8_RGBA_SRGB;
  anariSetParameter(d, frame, "channel.depth", ANARI_DATA_TYPE, &depthType);
  anariSetParameter(d, frame, "channel.primitiveId", ANARI_DATA_TYPE, &idType);
  anariSetParameter(d, frame, "channel.objectId", ANARI_DATA_TYPE, &idType);
  anariSetParameter(d, frame, "channel.color", ANARI_DATA_TYPE, &colorType);
  anariCommitParameters(d, frame);
  float scale[] = {1, 1, 1};
  for (int pass = 0; pass < 5; ++pass) {
    if (pass == 1) {
      attach(d, world, "surface", planeArray);
      anariCommitParameters(d, world);
    }
    if (pass == 2) {
      anariUnsetParameter(d, world, "surface");
      anariCommitParameters(d, world);
      scale[0] = 1.25f;
      scale[1] = .7f;
      scale[2] = 1.4f;
      float transform[] = {
          scale[0], 0, 0, 0, 0, scale[1], 0, 0, 0, 0, scale[2], 0, 0, 0, 0, 1};
      anariSetParameter(
          d, instance, "transform", ANARI_FLOAT32_MAT4, transform);
      anariCommitParameters(d, instance);
    }
    if (pass == 3) {
      std::swap(indices[0], indices[1]);
      radii[0] = .4f;
      radii[2] = .65f;
      centers[0][1] = -.25f;
      centers[2][1] = .2f;
      std::swap(colors[0][0], colors[0][1]);
      std::swap(colors[2][0], colors[2][1]);
      std::memcpy(anariMapArray(d, idx), indices, sizeof(indices));
      anariUnmapArray(d, idx);
      std::memcpy(anariMapArray(d, rad), radii, sizeof(radii));
      anariUnmapArray(d, rad);
      std::memcpy(anariMapArray(d, pos), centers, sizeof(centers));
      anariUnmapArray(d, pos);
      std::memcpy(anariMapArray(d, col), colors, sizeof(colors));
      anariUnmapArray(d, col);
    }
    if (pass == 4) {
      const uint32_t narrowIndices[] = {
          uint32_t(indices[0]), uint32_t(indices[1])};
      auto narrow =
          array(d, narrowIndices, ANARI_UINT32, 2, sizeof(narrowIndices));
      attach(d, geometry, "primitive.index", narrow);
      anariRelease(d, narrow);
      attach(d, geometry, "vertex.color", col);
      anariCommitParameters(d, geometry);
      anariSetParameter(d, material, "color", ANARI_STRING, "color");
      anariCommitParameters(d, material);
    }
    anariRenderFrame(d, frame);
    anariFrameReady(d, frame, ANARI_WAIT);
    uint32_t w, h;
    ANARIDataType type;
    const auto *depth = static_cast<const float *>(
        anariMapFrame(d, frame, "channel.depth", &w, &h, &type));
    const auto *object = static_cast<const uint32_t *>(
        anariMapFrame(d, frame, "channel.objectId", &w, &h, &type));
    const auto *primitive = static_cast<const uint32_t *>(
        anariMapFrame(d, frame, "channel.primitiveId", &w, &h, &type));
    const auto *color = static_cast<const unsigned char *>(
        anariMapFrame(d, frame, "channel.color", &w, &h, &type));
    check(depth && object && primitive && color && w == 192 && h == 192,
        "sphere channels map");
    if (!depth || !object || !primitive || !color)
      return 2;
    int hits = 0, wrongDepth = 0, wrongId = 0, wrongColor = 0;
    int expectedCoverage = 0, actualCoverage = 0;
    for (int y = 0; y < 192; ++y)
      for (int x = 0; x < 192; ++x) {
        float ray[] = {(2.f * (x + .5f) / 192 - 1) * std::tan(fovy / 2),
            (2.f * (y + .5f) / 192 - 1) * std::tan(fovy / 2),
            -1};
        const float length = std::sqrt(ray[0] * ray[0] + ray[1] * ray[1] + 1);
        for (auto &v : ray)
          v /= length;
        float near = pass == 1 ? -3.75f / ray[2]
                               : std::numeric_limits<float>::infinity();
        int selected = -1;
        float margin = 0;
        for (int i = 0; i < 2; ++i) {
          const int v = indices[i];
          float a = 0, b = 0, c = -radii[v] * radii[v];
          for (int k = 0; k < 3; ++k) {
            const float origin = eye[k] / scale[k] - centers[v][k];
            const float direction = ray[k] / scale[k];
            a += direction * direction;
            b += origin * direction;
            c += origin * origin;
          }
          const float disc = b * b - a * c;
          if (disc <= 0)
            continue;
          const float t = (-b - std::sqrt(disc)) / a;
          if (t < near) {
            near = t;
            selected = i;
            margin = disc;
          }
        }
        const int pixel = y * 192 + x;
        expectedCoverage += selected >= 0;
        actualCoverage += object[pixel] == sphereId;
        if (selected < 0 || margin < .04f
            || (pass == 1 && std::fabs(near + 3.75f / ray[2]) < .035f))
          continue;
        ++hits;
        wrongDepth += !std::isfinite(depth[pixel])
            || std::fabs(depth[pixel] - near) > .035f;
        wrongId +=
            object[pixel] != sphereId || primitive[pixel] != ids[selected];
        int component = colors[indices[selected]][0] > .5f ? 0 : 1;
        wrongColor += color[4 * pixel + component]
            <= color[4 * pixel + 1 - component] + 20;
      }
    std::printf(
        "pass %d: hits %d, depth errors %d, ID errors %d, color errors %d, coverage %d/%d\n",
        pass,
        hits,
        wrongDepth,
        wrongId,
        wrongColor,
        actualCoverage,
        expectedCoverage);
    check(hits > 500 && wrongDepth < hits / 100,
        "curved depth matches analytic ray/ellipsoid intersections");
    check(wrongId < hits / 100, "index order preserves original primitive IDs");
    check(std::abs(actualCoverage - expectedCoverage) < expectedCoverage / 20,
        "silhouette and plane occlusion match analytic spheres");
    if (storm)
      check(wrongColor < hits / 50,
          "indexed vertex attributes follow sphere selection and in-place edits");
    for (const char *channel : {"channel.depth",
             "channel.objectId",
             "channel.primitiveId",
             "channel.color"})
      anariUnmapFrame(d, frame, channel);
  }
  for (ANARIObject o : std::vector<ANARIObject>{frame,
           renderer,
           camera,
           world,
           instances,
           planeArray,
           instance,
           group,
           surfaces,
           plane,
           planeGeometry,
           planePos,
           sphere,
           material,
           geometry,
           pos,
           rad,
           col,
           idx,
           id})
    anariRelease(d, o);
  anariRelease(d, d);
  anariUnloadLibrary(lib);
  return errors ? 1 : 0;
}
