// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
#include <anari/anari.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
static int errors = 0;
static void check(bool ok, const char *what)
{
  std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
  errors += !ok;
}
static ANARIArray1D array(
    ANARIDevice d, const void *p, ANARIDataType t, size_t n, size_t bytes)
{
  auto a = anariNewArray1D(d, nullptr, nullptr, nullptr, t, n);
  std::memcpy(anariMapArray(d, a), p, bytes);
  anariUnmapArray(d, a);
  return a;
}
static void set(ANARIDevice d, ANARIObject o, const char *name, ANARIArray1D a)
{
  anariSetParameter(d, o, name, ANARI_ARRAY1D, &a);
  anariRelease(d, a);
}
int main(int argc, char **argv)
{
  const char *delegate = argc > 1 ? argv[1] : "storm";
  auto lib = anariLoadLibrary("hydra", nullptr, nullptr);
  if (!lib)
    return 2;
  auto d = anariNewDevice(lib, delegate);
  if (!d)
    return 2;
  auto camera = anariNewCamera(d, "perspective");
  float eye[] = {0, 0, 6}, direction[] = {0, 0, -1}, fovy = .8f;
  anariSetParameter(d, camera, "position", ANARI_FLOAT32_VEC3, eye);
  anariSetParameter(d, camera, "direction", ANARI_FLOAT32_VEC3, direction);
  anariSetParameter(d, camera, "fovy", ANARI_FLOAT32, &fovy);
  anariCommitParameters(d, camera);
  auto renderer = anariNewRenderer(d, "default");
  anariCommitParameters(d, renderer);
  auto frame = anariNewFrame(d);
  uint32_t size[] = {256, 256};
  ANARIDataType u32 = ANARI_UINT32;
  anariSetParameter(d, frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(d, frame, "camera", ANARI_CAMERA, &camera);
  anariSetParameter(d, frame, "renderer", ANARI_RENDERER, &renderer);
  const char *channels[] = {"channel.objectId",
      "channel.instanceId",
      "channel.primitiveId",
      "channel.aov:primId"};
  for (auto channel : channels)
    anariSetParameter(d, frame, channel, ANARI_DATA_TYPE, &u32);
  for (auto type : {"triangle",
           "quad",
           "cylinder",
           "cone",
           "sphere",
           "curve",
           "curve-unindexed"}) {
    if (std::strcmp(delegate, "storm")
        && (!std::strcmp(type, "curve")
            || !std::strcmp(type, "curve-unindexed")))
      continue;
    std::printf("Geometry: %s\n", type);
    std::vector<float> p;
    bool mesh = !std::strcmp(type, "triangle") || !std::strcmp(type, "quad");
    for (float x : {-.8f, .8f}) {
      if (mesh) {
        float v[] = {x - .3f,
            -.3f,
            0,
            x + .3f,
            -.3f,
            0,
            x + .3f,
            .3f,
            0,
            x - .3f,
            .3f,
            0};
        p.insert(p.end(), v, v + (!std::strcmp(type, "quad") ? 12 : 9));
      } else if (!std::strcmp(type, "sphere"))
        p.insert(p.end(), {x, 0, 0});
      else
        p.insert(p.end(), {x, -.3f, 0, x, .3f, 0});
    }
    const bool unindexedCurve = !std::strcmp(type, "curve-unindexed");
    if (unindexedCurve)
      p = {-1.1f, 0, 0, 0, 0, 0, 1.1f, 0, 0};
    auto geometry = anariNewGeometry(d, unindexedCurve ? "curve" : type);
    set(d,
        geometry,
        "vertex.position",
        array(d,
            p.data(),
            ANARI_FLOAT32_VEC3,
            p.size() / 3,
            p.size() * sizeof(float)));
    float radius = .23f;
    anariSetParameter(d, geometry, "radius", ANARI_FLOAT32, &radius);
    anariSetParameter(d, geometry, "caps", ANARI_STRING, "both");
    // Exercise primitive.radius/vertex.cap (this ticket) alongside picking: IDs
    // and attributes must still map through the generated triangles when the
    // new per-primitive/per-endpoint arrays are engaged, not just the global
    // 'radius'/'caps' fallbacks used above.
    if (!std::strcmp(type, "cylinder")) {
      float primRadii[] = {.23f, .23f};
      set(d,
          geometry,
          "primitive.radius",
          array(d, primRadii, ANARI_FLOAT32, 2, sizeof(primRadii)));
    }
    if (!std::strcmp(type, "cylinder") || !std::strcmp(type, "cone")) {
      uint8_t vertexCaps[] = {1, 1, 1, 1};
      set(d,
          geometry,
          "vertex.cap",
          array(d, vertexCaps, ANARI_UINT8, 4, sizeof(vertexCaps)));
    }
    if (!std::strcmp(type, "curve")) {
      uint32_t starts[] = {0, 2};
      set(d,
          geometry,
          "primitive.index",
          array(d, starts, ANARI_UINT32, 2, sizeof(starts)));
    }
    uint32_t primitiveIds[] = {0xa1234567u, 0xb2345678u};
    auto ids = array(d, primitiveIds, ANARI_UINT32, 2, sizeof(primitiveIds));
    anariSetParameter(d, geometry, "primitive.id", ANARI_ARRAY1D, &ids);
    anariCommitParameters(d, geometry);
    auto surface = anariNewSurface(d);
    uint32_t objectId = 0xe1234567u;
    anariSetParameter(d, surface, "geometry", ANARI_GEOMETRY, &geometry);
    anariSetParameter(d, surface, "id", ANARI_UINT32, &objectId);
    anariCommitParameters(d, surface);
    auto group = anariNewGroup(d);
    set(d,
        group,
        "surface",
        array(d, &surface, ANARI_SURFACE, 1, sizeof(surface)));
    anariCommitParameters(d, group);
    ANARIInstance instances[2];
    uint32_t instanceIds[] = {0xc1234567u, 0xd2345678u};
    for (int i = 0; i < 2; ++i) {
      instances[i] = anariNewInstance(d, "transform");
      float transform[] = {
          1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, i == 0 ? -1.2f : 1.2f, 0, 1};
      anariSetParameter(d, instances[i], "group", ANARI_GROUP, &group);
      anariSetParameter(
          d, instances[i], "transform", ANARI_FLOAT32_MAT4, transform);
      anariSetParameter(d, instances[i], "id", ANARI_UINT32, &instanceIds[i]);
      anariCommitParameters(d, instances[i]);
    }
    auto world = anariNewWorld(d);
    set(d,
        world,
        "surface",
        array(d, &surface, ANARI_SURFACE, 1, sizeof(surface)));
    set(d,
        world,
        "instance",
        array(d, instances, ANARI_INSTANCE, 2, sizeof(instances)));
    anariCommitParameters(d, world);
    anariSetParameter(d, frame, "world", ANARI_WORLD, &world);
    anariCommitParameters(d, frame);
    for (int pass = 0; pass < 3; ++pass) {
      if (pass == 1) {
        objectId = 42;
        instanceIds[0] = 71;
        primitiveIds[0] = 93;
        anariSetParameter(d, surface, "id", ANARI_UINT32, &objectId);
        anariCommitParameters(d, surface);
        anariSetParameter(d, instances[0], "id", ANARI_UINT32, &instanceIds[0]);
        anariCommitParameters(d, instances[0]);
        std::memcpy(anariMapArray(d, ids), primitiveIds, sizeof(primitiveIds));
        anariUnmapArray(d, ids);
      } else if (pass == 2) {
        objectId = ~0u;
        instanceIds[0] = instanceIds[1] = ~0u;
        primitiveIds[0] = 0;
        primitiveIds[1] = 1;
        anariUnsetParameter(d, surface, "id");
        anariCommitParameters(d, surface);
        for (auto instance : instances) {
          anariUnsetParameter(d, instance, "id");
          anariCommitParameters(d, instance);
        }
        anariUnsetParameter(d, geometry, "primitive.id");
        anariCommitParameters(d, geometry);
      }
      anariRenderFrame(d, frame);
      anariFrameReady(d, frame, ANARI_WAIT);
      const uint32_t *pixels[4];
      bool correct = true;
      int mismatchCount = 0;
      int coverage[3][2] = {};
      for (int c = 0; c < 4; ++c) {
        uint32_t w, h;
        ANARIDataType format;
        pixels[c] = static_cast<const uint32_t *>(
            anariMapFrame(d, frame, channels[c], &w, &h, &format));
        check(pixels[c] && w == 256 && h == 256 && format == ANARI_UINT32,
            channels[c]);
      }
      if (pixels[0] && pixels[1] && pixels[2] && pixels[3]) {
        for (int y = 0; y < 256; ++y)
          for (int x = 0; x < 256; ++x) {
            int pixel = y * 256 + x;
            if (x >= 2 && x < 254 && y >= 2 && y < 254) {
              bool interior = pixels[3][pixel] != ~0u;
              for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                  interior &=
                      pixels[3][(y + dy) * 256 + x + dx] == pixels[3][pixel];
              if (interior)
                correct &= pixels[2][pixel] != ~0u;
            }
            if (pixels[2][pixel] == ~0u) {
              bool match = pixels[0][pixel] == ~0u && pixels[1][pixel] == ~0u
                  && pixels[2][pixel] == ~0u;
              if (!match && mismatchCount++ < 3)
                std::printf("Miss mismatch (%d,%d): %08x %08x %08x\n",
                    x,
                    y,
                    pixels[0][pixel],
                    pixels[1][pixel],
                    pixels[2][pixel]);
              correct &= match;
            } else {
              int row = y < 95 ? 0 : y > 160 ? 2 : 1;
              int primitive = x < 128 ? 0 : 1;
              ++coverage[row][primitive];
              uint32_t instance =
                  row == 1 ? ~0u : instanceIds[row == 0 ? 0 : 1];
              // Consecutive segments overlap at their shared endpoint.
              const bool sharedEndpoint =
                  unindexedCurve && (x == 127 || x == 128);
              const bool primitiveMatches =
                  pixels[2][pixel] == primitiveIds[primitive]
                  || (sharedEndpoint
                      && (pixels[2][pixel] == primitiveIds[0]
                          || pixels[2][pixel] == primitiveIds[1]));
              bool match = pixels[0][pixel] == objectId
                  && pixels[1][pixel] == instance && primitiveMatches;
              if (!match && mismatchCount++ < 3)
                std::printf(
                    "Mismatch (%d,%d): object %08x/%08x instance %08x/%08x primitive %08x/%08x\n",
                    x,
                    y,
                    pixels[0][pixel],
                    objectId,
                    pixels[1][pixel],
                    instance,
                    pixels[2][pixel],
                    primitiveIds[primitive]);
              correct &= match;
            }
          }
      } else
        correct = false;
      check(correct, "every hit retains original IDs; misses retain sentinels");
      for (const auto &row : coverage)
        for (int count : row)
          check(count > 20,
              "direct/repeated instance original primitive covered");
      for (auto channel : channels)
        anariUnmapFrame(d, frame, channel);
    }
    anariUnsetParameter(d, frame, "channel.objectId");
    anariUnsetParameter(d, frame, "channel.aov:primId");
    for (int i = 0; i < 2; ++i) {
      instanceIds[i] = 71 + i;
      anariSetParameter(d, instances[i], "id", ANARI_UINT32, &instanceIds[i]);
      anariCommitParameters(d, instances[i]);
    }
    anariCommitParameters(d, frame);
    anariRenderFrame(d, frame);
    anariFrameReady(d, frame, ANARI_WAIT);
    for (const char *channel : {"channel.primitiveId", "channel.instanceId"}) {
      uint32_t w = 0, h = 0;
      ANARIDataType format = ANARI_UNKNOWN;
      const auto *values = static_cast<const uint32_t *>(
          anariMapFrame(d, frame, channel, &w, &h, &format));
      const bool valid =
          values && w == 256 && h == 256 && format == ANARI_UINT32;
      check(valid, "ID channel maps with hidden object-ID dependency");
      if (valid) {
        const uint32_t first =
            !std::strcmp(channel, "channel.primitiveId") ? 0 : 71;
        check(std::count(values, values + w * h, first) > 20
                && std::count(values, values + w * h, first + 1) > 20,
            "hidden object-ID dependency resolves both identities");
      }
      anariUnmapFrame(d, frame, channel);
    }
    for (const char *channel : {"channel.objectId", "channel.aov:primId"}) {
      uint32_t w = 0, h = 0;
      ANARIDataType format = ANARI_UNKNOWN;
      check(!anariMapFrame(d, frame, channel, &w, &h, &format),
          "hidden object-ID buffer is not exposed as an unrequested channel");
      anariSetParameter(d, frame, channel, ANARI_DATA_TYPE, &u32);
    }
    anariCommitParameters(d, frame);
    anariRelease(d, world);
    for (auto instance : instances)
      anariRelease(d, instance);
    anariRelease(d, group);
    anariRelease(d, surface);
    anariRelease(d, geometry);
    anariRelease(d, ids);
  }
  anariRelease(d, frame);
  anariRelease(d, renderer);
  anariRelease(d, camera);
  anariRelease(d, d);
  anariUnloadLibrary(lib);
  return errors ? 1 : 0;
}
