// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
#include <anari/anari.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static int errors = 0;
static void check(bool ok, const char *message)
{
  if (!ok) {
    ++errors;
    if (errors < 20)
      std::printf("FAIL: %s\n", message);
  }
}
static void status(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_ERROR) {
    ++errors;
    std::printf("ERROR: %s\n", message);
  }
}
int main(int argc, char **argv)
{
  auto lib = anariLoadLibrary("hydra", status, nullptr);
  auto d = anariNewDevice(lib, argc > 1 ? argv[1] : "storm");
  const float vertices[] = {
      -0.7f, -0.6f, 0, 0.9f, -0.6f, 0, 0.9f, 0.8f, 0, -0.7f, 0.8f, 0};
  auto a =
      anariNewArray1D(d, vertices, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  auto g = anariNewGeometry(d, "quad");
  anariSetParameter(d, g, "vertex.position", ANARI_ARRAY1D, &a);
  anariCommitParameters(d, g);
  auto m = anariNewMaterial(d, "matte");
  anariCommitParameters(d, m);
  auto s = anariNewSurface(d);
  anariSetParameter(d, s, "geometry", ANARI_GEOMETRY, &g);
  anariSetParameter(d, s, "material", ANARI_MATERIAL, &m);
  uint32_t id = 42;
  anariSetParameter(d, s, "id", ANARI_UINT32, &id);
  anariCommitParameters(d, s);
  auto surfaces = anariNewArray1D(d, &s, nullptr, nullptr, ANARI_SURFACE, 1);
  auto w = anariNewWorld(d);
  anariSetParameter(d, w, "surface", ANARI_ARRAY1D, &surfaces);
  anariCommitParameters(d, w);
  auto r = anariNewRenderer(d, "default");
  // Enough samples that a progressive delegate's per-render jitter cannot
  // flip a pixel's hit/miss between the full frame and a tile.
  int32_t pixelSamples = 16;
  anariSetParameter(d, r, "pixelSamples", ANARI_INT32, &pixelSamples);
  anariCommitParameters(d, r);
  auto f = anariNewFrame(d);
  anariSetParameter(d, f, "world", ANARI_WORLD, &w);
  anariSetParameter(d, f, "renderer", ANARI_RENDERER, &r);
  ANARIDataType depthType = ANARI_FLOAT32, idType = ANARI_UINT32;
  anariSetParameter(d, f, "channel.depth", ANARI_DATA_TYPE, &depthType);
  anariSetParameter(d, f, "channel.objectId", ANARI_DATA_TYPE, &idType);
  anariSetParameter(d, f, "channel.primitiveId", ANARI_DATA_TYPE, &idType);
  auto render = [&](uint32_t width, uint32_t height, bool commitFrame = true) {
    if (commitFrame) {
      uint32_t size[] = {width, height};
      anariSetParameter(d, f, "size", ANARI_UINT32_VEC2, size);
      anariCommitParameters(d, f);
    }
    anariRenderFrame(d, f);
    anariFrameReady(d, f, ANARI_WAIT);
    uint32_t mw, mh;
    ANARIDataType type;
    auto ptr = static_cast<const float *>(
        anariMapFrame(d, f, "channel.depth", &mw, &mh, &type));
    check(ptr && mw == width && mh == height, "depth shape");
    std::vector<float> result(ptr, ptr + width * height);
    anariUnmapFrame(d, f, "channel.depth");
    auto ids = static_cast<const uint32_t *>(
        anariMapFrame(d, f, "channel.objectId", &mw, &mh, &type));
    // Pick passes can differ at silhouette samples; check well inside coverage.
    for (uint32_t y = 1; y + 1 < height; ++y)
      for (uint32_t x = 1; x + 1 < width; ++x) {
        size_t i = y * width + x;
        if (std::isfinite(result[i]) && std::isfinite(result[i - 1])
            && std::isfinite(result[i + 1]) && std::isfinite(result[i - width])
            && std::isfinite(result[i + width]))
          check(ids[i] == 42, "picking aligns with camera projection");
      }
    anariUnmapFrame(d, f, "channel.objectId");
    return result;
  };
  const char **cameras = anariGetObjectSubtypes(d, ANARI_CAMERA);
  bool foundOrtho = false;
  for (auto p = cameras; p && *p; ++p)
    foundOrtho |= std::strcmp(*p, "orthographic") == 0;
  check(foundOrtho, "orthographic subtype is discoverable");
  for (const char *subtype : {"perspective", "orthographic"}) {
    bool ortho = std::strcmp(subtype, "orthographic") == 0;
    auto c = anariNewCamera(d, subtype);
    float eye[] = {0, 0, 3};
    anariSetParameter(d, c, "position", ANARI_FLOAT32_VEC3, eye);
    // Exercise standard default fovy=60 degrees / height=1 first.
    anariCommitParameters(d, c);
    anariSetParameter(d, f, "camera", ANARI_CAMERA, &c);
    for (uint32_t width : {96u, 160u})
      for (bool explicitAspect : {false, true}) {
        float aspect = 1.25f;
        if (explicitAspect)
          anariSetParameter(d, c, "aspect", ANARI_FLOAT32, &aspect);
        else
          anariUnsetParameter(d, c, "aspect");
        anariUnsetParameter(d, c, "imageRegion");
        anariCommitParameters(d, c);
        int initialErrors = errors;
        auto full = render(width, 96);
        double halfY = ortho ? 0.5 : std::sqrt(3.0);
        double halfX = halfY * (explicitAspect ? aspect : double(width) / 96);
        for (uint32_t y = 0; y < 96; ++y)
          for (uint32_t x = 0; x < width; ++x) {
            double px = (2 * (x + 0.5) / width - 1) * halfX;
            double py = (2 * (y + 0.5) / 96 - 1) * halfY;
            // Skip pixel footprints crossing a silhouette (MSAA differs by
            // delegate).
            if (std::fabs(px + 0.7) < 2 * halfX / width
                || std::fabs(px - 0.9) < 2 * halfX / width
                || std::fabs(py + 0.6) < 2 * halfY / 96
                || std::fabs(py - 0.8) < 2 * halfY / 96)
              continue;
            bool hit = px > -.7 && px < .9 && py > -.6 && py < .8;
            float value = full[y * width + x];
            check(std::isfinite(value) == hit,
                "default projection and aspect coverage");
            if (hit)
              check(std::fabs(
                        value - (ortho ? 3 : std::sqrt(9 + px * px + py * py)))
                      < 0.012,
                  "ray distance");
          }
        // Explicit full-image aspect makes unequal-size tiles reproduce full
        // image.
        aspect = explicitAspect ? aspect : float(width) / 96;
        anariSetParameter(d, c, "aspect", ANARI_FLOAT32, &aspect);
        const uint32_t xs[] = {0, width / 4, width}, ys[] = {0, 32, 96};
        for (int ty = 0; ty < 2; ++ty)
          for (int tx = 0; tx < 2; ++tx) {
            float region[] = {float(xs[tx]) / width,
                float(ys[ty]) / 96,
                float(xs[tx + 1]) / width,
                float(ys[ty + 1]) / 96};
            anariSetParameter(d, c, "imageRegion", ANARI_FLOAT32_BOX2, region);
            anariCommitParameters(d, c);
            uint32_t tw = xs[tx + 1] - xs[tx], th = ys[ty + 1] - ys[ty];
            auto tile = render(tw, th);
            for (uint32_t y = 0; y < th; ++y)
              for (uint32_t x = 0; x < tw; ++x) {
                uint32_t fx = x + xs[tx], fy = y + ys[ty];
                float before = full[fy * width + fx], after = tile[y * tw + x];
                // Progressive delegates jitter silhouette samples independently
                // per render. Compare pixels whose full-frame neighborhood
                // agrees.
                bool interior =
                    fx > 0 && fx + 1 < width && fy > 0 && fy + 1 < 96;
                if (!interior)
                  continue;
                bool hit = std::isfinite(before);
                if (std::isfinite(full[fy * width + fx - 1]) != hit
                    || std::isfinite(full[fy * width + fx + 1]) != hit
                    || std::isfinite(full[(fy - 1) * width + fx]) != hit
                    || std::isfinite(full[(fy + 1) * width + fx]) != hit)
                  continue;
                check((std::isinf(before) && std::isinf(after))
                        || std::fabs(before - after) < 0.012,
                    "asymmetric tiles reproduce full-frame depth");
              }
          }
        std::printf("%s width=%u explicit=%d errors=%d\n",
            subtype,
            width,
            explicitAspect,
            errors - initialErrors);
      }
    // Camera-only recommit: no frame commit before rendering.
    anariUnsetParameter(d, c, "imageRegion");
    eye[2] = 4;
    anariSetParameter(d, c, "position", ANARI_FLOAT32_VEC3, eye);
    anariCommitParameters(d, c);
    anariRenderFrame(d, f);
    anariFrameReady(d, f, ANARI_WAIT);
    uint32_t mw, mh;
    ANARIDataType type;
    auto depth = static_cast<const float *>(
        anariMapFrame(d, f, "channel.depth", &mw, &mh, &type));
    check(std::fabs(depth[(mh / 2) * mw + mw / 2] - 4) < 0.02,
        "camera recommit affects next frame");
    anariUnmapFrame(d, f, "channel.depth");
    // Change committed geometry bounds without a world/frame recommit.
    anariUnsetParameter(d, c, "aspect");
    eye[2] = 0;
    anariSetParameter(d, c, "position", ANARI_FLOAT32_VEC3, eye);
    const auto params = static_cast<const ANARIParameter *>(anariGetObjectInfo(
        d, ANARI_CAMERA, subtype, "parameter", ANARI_PARAMETER_LIST));
    bool clippingFound = false;
    for (auto p = params; p && p->name; ++p)
      clippingFound |= std::strcmp(p->name, "hydra.clippingRange") == 0
          && p->type == ANARI_FLOAT32_VEC2;
    check(clippingFound, "clipping override reflected");
    for (float distance : {0.001f, 100000.f, 1.f}) {
      float points[] = {-distance,
          -distance,
          -distance,
          distance,
          -distance,
          -distance,
          distance,
          distance,
          -distance,
          -distance,
          distance,
          -distance};
      auto positions =
          anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
      std::memcpy(anariMapArray(d, positions), points, sizeof(points));
      anariUnmapArray(d, positions);
      anariSetParameter(d, g, "vertex.position", ANARI_ARRAY1D, &positions);
      anariCommitParameters(d, g);
      float height = distance;
      anariSetParameter(d, c, "height", ANARI_FLOAT32, &height);
      anariCommitParameters(d, c);
      auto depths = render(32, 32, distance == 0.001f);
      double expected =
          ortho ? distance : distance * std::sqrt(1 + 2.0 / (32 * 32 * 3));
      std::printf("scale %s distance=%g depth=%g expected=%g\n",
          subtype,
          distance,
          depths[16 * 32 + 16],
          expected);
      check(std::fabs(depths[16 * 32 + 16] - expected) < distance * 0.002,
          "adaptive clipping and depth across scene scales");
      anariRelease(d, positions);
    }
    for (float secondDepth : {-1.f, 10000.f}) {
      float points[] = {-1,
          -1,
          -1,
          0,
          -1,
          -1,
          0,
          1,
          -1,
          -1,
          1,
          -1,
          -10000,
          -10000,
          -secondDepth,
          10000,
          -10000,
          -secondDepth,
          10000,
          10000,
          -secondDepth,
          -10000,
          10000,
          -secondDepth};
      auto positions =
          anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_FLOAT32_VEC3, 8);
      std::memcpy(anariMapArray(d, positions), points, sizeof(points));
      anariUnmapArray(d, positions);
      anariSetParameter(d, g, "vertex.position", ANARI_ARRAY1D, &positions);
      anariCommitParameters(d, g);
      auto depths = render(32, 32, false);
      const double rayFactor = ortho
          ? 1.0
          : std::sqrt(1 + (0.46875 * 0.46875 + 0.03125 * 0.03125) / 3.0);
      std::printf("span %s second=%g nearDepth=%g expected=%g farDepth=%g\n",
          subtype,
          secondDepth,
          depths[16 * 32 + 8],
          rayFactor,
          depths[16 * 32 + 23]);
      check(std::fabs(depths[16 * 32 + 8] - rayFactor) < 0.01,
          "near geometry survives eye-crossing and wide scene bounds");
      if (secondDepth > 0)
        check(std::fabs(depths[16 * 32 + 23] - 10000 * rayFactor) < 20,
            "simultaneous far geometry retains usable depth precision");
      anariRelease(d, positions);
    }
    float unitPoints[] = {-1, -1, -1, 1, -1, -1, 1, 1, -1, -1, 1, -1};
    auto unitPositions =
        anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
    std::memcpy(
        anariMapArray(d, unitPositions), unitPoints, sizeof(unitPoints));
    anariUnmapArray(d, unitPositions);
    anariSetParameter(d, g, "vertex.position", ANARI_ARRAY1D, &unitPositions);
    anariCommitParameters(d, g);
    anariRelease(d, unitPositions);
    float overrideRange[] = {2, 4};
    anariSetParameter(
        d, c, "hydra.clippingRange", ANARI_FLOAT32_VEC2, overrideRange);
    anariCommitParameters(d, c);
    auto clipped = render(32, 32);
    check(std::isinf(clipped[16 * 32 + 16]),
        "explicit near plane clips geometry");
    anariUnsetParameter(d, c, "hydra.clippingRange");
    anariCommitParameters(d, c);
    anariUnsetParameter(d, w, "surface");
    anariCommitParameters(d, w);
    auto empty = render(32, 32);
    check(std::isinf(empty[16 * 32 + 16]), "empty world clipping fallback");
    anariSetParameter(d, w, "surface", ANARI_ARRAY1D, &surfaces);
    anariCommitParameters(d, w);
    anariSetParameter(d, g, "vertex.position", ANARI_ARRAY1D, &a);
    anariCommitParameters(d, g);
    anariRelease(d, c);
  }
  for (ANARIObject o : std::vector<ANARIObject>{f, r, w, surfaces, s, m, g, a})
    anariRelease(d, o);
  anariRelease(d, d);
  anariUnloadLibrary(lib);
  std::printf("camera: %d errors\n", errors);
  return errors ? 1 : 0;
}
