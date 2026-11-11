// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Smoke test: render a simple triangle mesh and validate output pixels.
//
// Usage: ./smoke_test [library]   (default: helide_gpu)

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#define ANARI_EXTENSION_UTILITY_IMPL
#include "anari/anari_cpp.hpp"
#include "anari/anari_cpp/ext/std.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using vec3 = std::array<float, 3>;
using vec4 = std::array<float, 4>;
using uvec2 = std::array<unsigned int, 2>;
using uvec3 = std::array<unsigned int, 3>;

// ---------------------------------------------------------------------------
// ANARI status callback
// ---------------------------------------------------------------------------

static void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  const char *prefix = "[????]";
  if (severity == ANARI_SEVERITY_FATAL_ERROR)
    prefix = "[FATAL]";
  else if (severity == ANARI_SEVERITY_ERROR)
    prefix = "[ERROR]";
  else if (severity == ANARI_SEVERITY_WARNING)
    prefix = "[WARN ]";
  else if (severity == ANARI_SEVERITY_INFO)
    prefix = "[INFO ]";
  else if (severity == ANARI_SEVERITY_DEBUG)
    prefix = "[DEBUG]";
  fprintf(stderr, "%s %s\n", prefix, message);
}

// ---------------------------------------------------------------------------
// Pixel helpers
// ---------------------------------------------------------------------------

struct RGBA8
{
  uint8_t r, g, b, a;
};

static RGBA8 samplePixel(
    const RGBA8 *buf, uvec2 size, unsigned int x, unsigned int y)
{
  return buf[y * size[0] + x];
}

static float luminance(RGBA8 p)
{
  return (0.2126f * p.r + 0.7152f * p.g + 0.0722f * p.b) / 255.f;
}

// ---------------------------------------------------------------------------
// Regression render for one geometry: must not hang, must produce output.
// ---------------------------------------------------------------------------

static bool renderGeometryScene(anari::Device d,
    const char *label,
    anari::Geometry geom,
    uvec2 imgSize,
    float aspect)
{
  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  vec3 camPos = {0.5f, 0.5f, 3.f};
  vec3 camDir = {0.f, 0.f, -1.f};
  vec3 camUp = {0.f, 1.f, 0.f};
  anari::setParameter(d, camera, "position", camPos);
  anari::setParameter(d, camera, "direction", camDir);
  anari::setParameter(d, camera, "up", camUp);
  anari::setParameter(d, camera, "fovy", float(M_PI / 3.0));
  anari::setParameter(d, camera, "aspect", aspect);
  anari::commitParameters(d, camera);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::release(d, surface);
  anari::commitParameters(d, world);

  auto renderer = anari::newObject<anari::Renderer>(d, "default");
  vec4 bg = {0.0f, 0.0f, 0.0f, 1.f};
  anari::setParameter(d, renderer, "background", bg);
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", imgSize);
  anari::setParameter(d, frame, "channel.color", ANARI_UFIXED8_VEC4);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  printf("[smoke_test] %s: rendering...\n", label);
  anari::render(d, frame);
  anari::wait(d, frame);
  printf("[smoke_test] %s: done\n", label);

  auto fb = anari::map<RGBA8>(d, frame, "channel.color");
  bool ok = fb.data != nullptr;
  if (ok) {
    std::string outFile = std::string("smoke_") + label + ".png";
    stbi_flip_vertically_on_write(1);
    stbi_write_png(outFile.c_str(),
        int(fb.width),
        int(fb.height),
        4,
        fb.data,
        4 * int(fb.width));
    printf("[smoke_test] %s: wrote %s\n", label, outFile.c_str());

    const float lumCenter = luminance(
        samplePixel(fb.data, imgSize, imgSize[0] / 2, imgSize[1] / 2));
    const float lumCornerBL = luminance(samplePixel(fb.data, imgSize, 4, 4));
    const float lumCornerTR = luminance(
        samplePixel(fb.data, imgSize, imgSize[0] - 5, imgSize[1] - 5));

    const float litThreshold = 0.05f;
    const float bgThreshold = 0.05f;
    ok = lumCenter > litThreshold && lumCornerBL < bgThreshold
        && lumCornerTR < bgThreshold;

    printf("[smoke_test] %s: center=%.3f cornerBL=%.3f cornerTR=%.3f => %s\n",
        label,
        lumCenter,
        lumCornerBL,
        lumCornerTR,
        ok ? "PASS" : "FAIL");
  }
  anari::unmap(d, frame, "channel.color");

  anari::release(d, frame);
  return ok;
}

// ---------------------------------------------------------------------------
// faceVarying attributes: an indexed quad whose two triangles share vertices 0
// and 2 but carry different per-corner colors and normals, which must override
// the per-vertex ones. Corner values indexed by vertex instead of by
// 3 * primitive + corner would bleed between the triangles.
// ---------------------------------------------------------------------------

static bool renderFaceVaryingScene(anari::Device d, uvec2 imgSize, float aspect)
{
  const char *label = "faceVarying";

  // Camera at (0.5, 0.5, 3) with a 60 degree fovy sees +-1.732 around the
  // quad's center at z = 0.
  vec3 positions[] = {
      {-0.5f, -0.5f, 0.f},
      {1.5f, -0.5f, 0.f},
      {1.5f, 1.5f, 0.f},
      {-0.5f, 1.5f, 0.f},
  };
  uvec3 indices[] = {{0, 1, 2}, {0, 2, 3}};
  vec4 vertexColors[] = {
      {1.f, 1.f, 1.f, 1.f},
      {1.f, 1.f, 1.f, 1.f},
      {1.f, 1.f, 1.f, 1.f},
      {1.f, 1.f, 1.f, 1.f},
  };
  vec3 vertexNormals[] = {
      {0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}};
  // Triangle 0 is red; triangle 1 is green except blue at its corner 2
  vec4 fvColors[] = {
      {1.f, 0.f, 0.f, 1.f},
      {1.f, 0.f, 0.f, 1.f},
      {1.f, 0.f, 0.f, 1.f},
      {0.f, 1.f, 0.f, 1.f},
      {0.f, 1.f, 0.f, 1.f},
      {0.f, 0.f, 1.f, 1.f},
  };
  // Triangle 0 faces the camera; triangle 1 tilts 45 degrees toward +x
  const float s = std::sqrt(0.5f);
  vec3 fvNormals[] = {
      {0.f, 0.f, 1.f},
      {0.f, 0.f, 1.f},
      {0.f, 0.f, 1.f},
      {s, 0.f, s},
      {s, 0.f, s},
      {s, 0.f, s},
  };

  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", positions, 4);
  anari::setParameterArray1D(d, geom, "vertex.normal", vertexNormals, 4);
  anari::setParameterArray1D(d, geom, "vertex.color", vertexColors, 4);
  anari::setParameterArray1D(d, geom, "primitive.index", indices, 2);
  anari::setParameterArray1D(d, geom, "faceVarying.color", fvColors, 6);
  anari::setParameterArray1D(d, geom, "faceVarying.normal", fvNormals, 6);
  anari::commitParameters(d, geom);

  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  vec3 camPos = {0.5f, 0.5f, 3.f};
  vec3 camDir = {0.f, 0.f, -1.f};
  vec3 camUp = {0.f, 1.f, 0.f};
  anari::setParameter(d, camera, "position", camPos);
  anari::setParameter(d, camera, "direction", camDir);
  anari::setParameter(d, camera, "up", camUp);
  anari::setParameter(d, camera, "fovy", float(M_PI / 3.0));
  anari::setParameter(d, camera, "aspect", aspect);
  anari::commitParameters(d, camera);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", "color");
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::release(d, surface);
  anari::commitParameters(d, world);

  auto renderer = anari::newObject<anari::Renderer>(d, "default");
  vec4 bg = {0.0f, 0.0f, 0.0f, 1.f};
  anari::setParameter(d, renderer, "background", bg);
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", imgSize);
  anari::setParameter(d, frame, "channel.color", ANARI_UFIXED8_VEC4);
  anari::setParameter(d, frame, "channel.normal", ANARI_FLOAT32_VEC3);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  printf("[smoke_test] %s: rendering...\n", label);
  anari::render(d, frame);
  anari::wait(d, frame);

  // World point -> pixel (origin bottom-left)
  const float halfExtent = 3.f * std::tan(float(M_PI / 6.0));
  auto toPixel = [&](float wx, float wy) {
    return std::array<unsigned int, 2>{
        (unsigned int)(imgSize[0] * (0.5f + 0.5f * (wx - 0.5f) / halfExtent)),
        (unsigned int)(imgSize[1] * (0.5f + 0.5f * (wy - 0.5f) / halfExtent))};
  };
  struct Sample
  {
    const char *name;
    float wx, wy;
    int dominant; // 0 = r, 1 = g, 2 = b
    float normalX; // expected world normal x
  };
  // Triangle 1 points by barycentrics over (v0, v2, v3): (0.45, 0.45, 0.1)
  // and (0.1, 0.1, 0.8)
  const Sample samples[] = {
      {"tri0", 1.2f, -0.2f, 0, 0.f},
      {"tri1 corners 0,1", 0.4f, 0.6f, 1, s},
      {"tri1 corner 2", -0.3f, 1.3f, 2, s},
  };

  bool ok = true;
  auto fb = anari::map<RGBA8>(d, frame, "channel.color");
  auto nb = anari::map<vec3>(d, frame, "channel.normal");
  if (!fb.data || !nb.data) {
    ok = false;
  } else {
    std::string outFile = std::string("smoke_") + label + ".png";
    stbi_flip_vertically_on_write(1);
    stbi_write_png(outFile.c_str(),
        int(fb.width),
        int(fb.height),
        4,
        fb.data,
        4 * int(fb.width));

    for (const auto &smp : samples) {
      const auto px = toPixel(smp.wx, smp.wy);
      const RGBA8 c = samplePixel(fb.data, imgSize, px[0], px[1]);
      const vec3 n = nb.data[px[1] * imgSize[0] + px[0]];
      const int ch[3] = {c.r, c.g, c.b};
      const int dom = ch[smp.dominant];
      const bool colorOk = dom > 32 && dom > 2 * ch[(smp.dominant + 1) % 3]
          && dom > 2 * ch[(smp.dominant + 2) % 3];
      const bool normalOk = std::fabs(n[0] - smp.normalX) < 0.05f;
      printf(
          "[smoke_test] %s: %-16s [%3u,%3u] rgb=(%3u,%3u,%3u) "
          "n=(%.2f,%.2f,%.2f) => %s\n",
          label,
          smp.name,
          px[0],
          px[1],
          c.r,
          c.g,
          c.b,
          n[0],
          n[1],
          n[2],
          colorOk && normalOk ? "PASS" : "FAIL");
      ok = ok && colorOk && normalOk;
    }
  }
  if (fb.data)
    anari::unmap(d, frame, "channel.color");
  if (nb.data)
    anari::unmap(d, frame, "channel.normal");

  anari::release(d, frame);
  return ok;
}

// ---------------------------------------------------------------------------
// Quad light: a matte floor at z = 0 lit only by a quad light one unit above
// it. Returns the floor's color at the image center, below the quad's center.
// ---------------------------------------------------------------------------

struct QuadLightSetup
{
  vec3 position;
  vec3 edge1;
  vec3 edge2;
  const char *side;
  const char *brightnessParam; // "intensity", "power" or "radiance"
  float brightness;
};

static RGBA8 renderQuadLightScene(
    anari::Device d, uvec2 imgSize, float aspect, const QuadLightSetup &q)
{
  vec3 positions[] = {
      {-3.f, -3.f, 0.f}, {4.f, -3.f, 0.f}, {4.f, 4.f, 0.f}, {-3.f, 4.f, 0.f}};
  uvec3 indices[] = {{0, 1, 2}, {0, 2, 3}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", positions, 4);
  anari::setParameterArray1D(d, geom, "primitive.index", indices, 2);
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);

  auto light = anari::newObject<anari::Light>(d, "quad");
  anari::setParameter(d, light, "position", q.position);
  anari::setParameter(d, light, "edge1", q.edge1);
  anari::setParameter(d, light, "edge2", q.edge2);
  anari::setParameter(d, light, "side", q.side);
  anari::setParameter(d, light, q.brightnessParam, q.brightness);
  anari::commitParameters(d, light);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::setParameterArray1D(d, world, "light", &light, 1);
  anari::release(d, surface);
  anari::release(d, light);
  anari::commitParameters(d, world);

  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  vec3 camPos = {0.5f, 0.5f, 3.f};
  vec3 camDir = {0.f, 0.f, -1.f};
  vec3 camUp = {0.f, 1.f, 0.f};
  anari::setParameter(d, camera, "position", camPos);
  anari::setParameter(d, camera, "direction", camDir);
  anari::setParameter(d, camera, "up", camUp);
  anari::setParameter(d, camera, "fovy", float(M_PI / 3.0));
  anari::setParameter(d, camera, "aspect", aspect);
  anari::commitParameters(d, camera);

  auto renderer = anari::newObject<anari::Renderer>(d, "default");
  vec4 bg = {0.0f, 0.0f, 0.0f, 1.f};
  anari::setParameter(d, renderer, "background", bg);
  anari::setParameter(d, renderer, "ignoreAmbientLighting", false);
  anari::setParameter(d, renderer, "ambientRadiance", 0.f);
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", imgSize);
  anari::setParameter(d, frame, "channel.color", ANARI_UFIXED8_VEC4);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  anari::render(d, frame);
  anari::wait(d, frame);

  RGBA8 center{};
  auto fb = anari::map<RGBA8>(d, frame, "channel.color");
  if (fb.data) {
    center = samplePixel(fb.data, imgSize, imgSize[0] / 2, imgSize[1] / 2);
    anari::unmap(d, frame, "channel.color");
  }
  anari::release(d, frame);
  return center;
}

static bool renderQuadLightScenes(anari::Device d, uvec2 imgSize, float aspect)
{
  const float pi = float(M_PI);
  // One unit above the floor, this intensity lights it well below clipping
  const float I = pi / 4.f;
  // A unit quad centered at (0.5, 0.5, 1); cross(edge2, edge1) is -z, so
  // its front faces the floor
  const vec3 pos = {0.f, 0.f, 1.f};
  const vec3 e1 = {1.f, 0.f, 0.f};
  const vec3 e2 = {0.f, 1.f, 0.f};
  // The same center with twice the edges: four times the area
  const vec3 pos4 = {-0.5f, -0.5f, 1.f};
  const vec3 e1x2 = {2.f, 0.f, 0.f};
  const vec3 e2x2 = {0.f, 2.f, 0.f};

  const RGBA8 front = renderQuadLightScene(
      d, imgSize, aspect, {pos, e1, e2, "front", "intensity", I});
  const RGBA8 back = renderQuadLightScene(
      d, imgSize, aspect, {pos, e1, e2, "back", "intensity", I});
  const RGBA8 both = renderQuadLightScene(
      d, imgSize, aspect, {pos, e1, e2, "both", "intensity", I});
  // radiance * area == intensity along the normal
  const RGBA8 radiance = renderQuadLightScene(
      d, imgSize, aspect, {pos4, e1x2, e2x2, "front", "radiance", I / 4.f});
  // a Lambertian side emits pi * intensity
  const RGBA8 power = renderQuadLightScene(
      d, imgSize, aspect, {pos, e1, e2, "front", "power", pi * I});
  // twice the power over two sides is the same intensity
  const RGBA8 powerBoth = renderQuadLightScene(
      d, imgSize, aspect, {pos, e1, e2, "both", "power", 2.f * pi * I});

  auto close = [](RGBA8 a, RGBA8 b) {
    return std::abs(int(a.r) - int(b.r)) <= 2
        && std::abs(int(a.g) - int(b.g)) <= 2
        && std::abs(int(a.b) - int(b.b)) <= 2;
  };
  struct Check
  {
    const char *name;
    RGBA8 value;
    bool ok;
  };
  const Check checks[] = {
      {"front lit", front, luminance(front) > 0.1f},
      {"back unlit", back, luminance(back) < 0.02f},
      {"both == front", both, close(both, front)},
      {"radiance*area == intensity", radiance, close(radiance, front)},
      {"power == pi*intensity", power, close(power, front)},
      {"power both sides", powerBoth, close(powerBoth, front)},
  };
  bool ok = true;
  for (const auto &c : checks) {
    printf("[smoke_test] quadLight: %-28s rgb=(%3u,%3u,%3u) => %s\n",
        c.name,
        c.value.r,
        c.value.g,
        c.value.b,
        c.ok ? "PASS" : "FAIL");
    ok = ok && c.ok;
  }
  return ok;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, const char **argv)
{
  const char *libName = (argc > 1) ? argv[1] : "helide_gpu";

  const uvec2 imgSize = {512u, 512u};
  const float aspect = float(imgSize[0]) / float(imgSize[1]);

  // Large triangle in front of the camera, spanning most of the view.
  //   Camera at (0,0,3) looking toward -Z.
  //   Triangle lies in the Z=0 plane.
  vec3 positions[] = {
      {-1.5f, -1.5f, 0.f},
      {1.5f, -1.5f, 0.f},
      {0.f, 1.5f, 0.f},
  };
  // Explicit face normals pointing toward +Z (toward the camera)
  vec3 normals[] = {
      {0.f, 0.f, 1.f},
      {0.f, 0.f, 1.f},
      {0.f, 0.f, 1.f},
  };

  printf("[smoke_test] library = %s\n", libName);
  printf("[smoke_test] image   = %u x %u\n", imgSize[0], imgSize[1]);

  // --- Load library & create device ---
  anari::Library lib = anari::loadLibrary(libName, statusFunc);
  if (!lib) {
    fprintf(
        stderr, "[smoke_test] FATAL: could not load library '%s'\n", libName);
    return 1;
  }

  anari::Device d = anari::newDevice(lib, "default");
  if (!d) {
    fprintf(stderr, "[smoke_test] FATAL: could not create device\n");
    anari::unloadLibrary(lib);
    return 1;
  }

  // --- Camera ---
  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  vec3 camPos = {0.f, 0.f, 3.f};
  vec3 camDir = {0.f, 0.f, -1.f};
  vec3 camUp = {0.f, 1.f, 0.f};
  anari::setParameter(d, camera, "position", camPos);
  anari::setParameter(d, camera, "direction", camDir);
  anari::setParameter(d, camera, "up", camUp);
  anari::setParameter(d, camera, "fovy", float(M_PI / 3.0)); // 60 degrees
  anari::setParameter(d, camera, "aspect", aspect);
  anari::commitParameters(d, camera);

  // --- Geometry ---
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", positions, 3);
  anari::setParameterArray1D(d, geom, "vertex.normal", normals, 3);
  anari::commitParameters(d, geom);

  // --- Material ---
  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::commitParameters(d, mat);

  // --- Surface ---
  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);

  // --- World (surfaces placed directly, no instance) ---
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::release(d, surface);
  anari::commitParameters(d, world);

  // --- Renderer ---
  auto renderer = anari::newObject<anari::Renderer>(d, "default");
  vec4 bg = {0.0f, 0.0f, 0.0f, 1.f};
  anari::setParameter(d, renderer, "background", bg);
  anari::commitParameters(d, renderer);

  // --- Frame ---
  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", imgSize);
  anari::setParameter(d, frame, "channel.color", ANARI_UFIXED8_VEC4);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  // --- Render ---
  printf("[smoke_test] rendering...\n");
  anari::render(d, frame);
  anari::wait(d, frame);
  printf("[smoke_test] done.\n");

  // --- Read pixels ---
  auto fb = anari::map<RGBA8>(d, frame, "channel.color");
  if (!fb.data) {
    fprintf(stderr, "[smoke_test] FATAL: failed to map color channel\n");
    anari::release(d, frame);
    anari::release(d, d);
    anari::unloadLibrary(lib);
    return 1;
  }

  // Save PNG (flip vertically: ANARI origin is bottom-left)
  stbi_flip_vertically_on_write(1);
  std::string outFile = std::string("smoke_") + libName + ".png";
  stbi_write_png(outFile.c_str(),
      int(fb.width),
      int(fb.height),
      4,
      fb.data,
      4 * int(fb.width));
  printf("[smoke_test] wrote %s\n", outFile.c_str());

  // Sample key pixels and report luminance
  auto reportPixel = [&](const char *label, unsigned int x, unsigned int y) {
    RGBA8 p = samplePixel(fb.data, imgSize, x, y);
    float lum = luminance(p);
    printf("  %-20s [%3u,%3u]  rgba=(%3u,%3u,%3u,%3u)  lum=%.3f\n",
        label,
        x,
        y,
        p.r,
        p.g,
        p.b,
        p.a,
        lum);
    return lum;
  };

  printf("[smoke_test] pixel samples:\n");
  float lumCenter = reportPixel("center", imgSize[0] / 2, imgSize[1] / 2);
  float lumCornerBL = reportPixel("corner(BL)", 4, 4);
  float lumCornerTR = reportPixel("corner(TR)", imgSize[0] - 5, imgSize[1] - 5);

  anari::unmap(d, frame, "channel.color");

  // --- Validate ---
  // The triangle covers the center; corners should be background (black).
  const float litThreshold = 0.05f; // center must be at least this bright
  const float bgThreshold = 0.05f; // corners must be at most this bright

  bool centerLit = lumCenter > litThreshold;
  bool cornersDark = lumCornerBL < bgThreshold && lumCornerTR < bgThreshold;

  printf("[smoke_test] results:\n");
  printf("  center lit  (lum > %.2f): %s\n",
      litThreshold,
      centerLit ? "PASS" : "FAIL");
  printf("  corners dark (lum < %.2f): %s\n",
      bgThreshold,
      cornersDark ? "PASS" : "FAIL");

  bool passed = centerLit && cornersDark;

  // Render a capped cylinder through the shared triangle-mesh path.
  vec3 cylinderPositions[] = {
      {0.5f, 0.0f, 0.0f},
      {0.5f, 1.0f, 0.0f},
  };
  auto cylinder = anari::newObject<anari::Geometry>(d, "cylinder");
  anari::setParameterArray1D(
      d, cylinder, "vertex.position", cylinderPositions, 2);
  anari::setParameter(d, cylinder, "radius", 0.25f);
  anari::setParameter(d, cylinder, "caps", "both");
  anari::commitParameters(d, cylinder);
  const bool cylinderPassed =
      renderGeometryScene(d, "cylinder", cylinder, imgSize, aspect);
  anari::release(d, cylinder);

  vec3 conePositions[] = {
      {0.5f, 0.0f, 0.0f},
      {0.5f, 1.0f, 0.0f},
  };
  float coneRadii[] = {0.3f, 0.0f};
  uint8_t coneCaps[] = {1u, 0u};
  auto cone = anari::newObject<anari::Geometry>(d, "cone");
  anari::setParameterArray1D(d, cone, "vertex.position", conePositions, 2);
  anari::setParameterArray1D(d, cone, "vertex.radius", coneRadii, 2);
  anari::setParameterArray1D(d, cone, "vertex.cap", coneCaps, 2);
  anari::setParameter(d, cone, "caps", "none");
  anari::commitParameters(d, cone);
  const bool conePassed = renderGeometryScene(d, "cone", cone, imgSize, aspect);
  anari::release(d, cone);

  vec3 curvePositions[] = {
      {0.5f, 0.0f, 0.0f},
      {0.5f, 1.0f, 0.0f},
  };
  float curveRadii[] = {0.2f, 0.1f};
  auto curve = anari::newObject<anari::Geometry>(d, "curve");
  anari::setParameterArray1D(d, curve, "vertex.position", curvePositions, 2);
  anari::setParameterArray1D(d, curve, "vertex.radius", curveRadii, 2);
  anari::commitParameters(d, curve);
  const bool curvePassed =
      renderGeometryScene(d, "curve", curve, imgSize, aspect);
  anari::release(d, curve);

  const bool faceVaryingPassed = renderFaceVaryingScene(d, imgSize, aspect);

  // --- Cleanup ---
  anari::release(d, frame);

  const bool quadLightPassed = renderQuadLightScenes(d, imgSize, aspect);

  passed = passed && cylinderPassed && conePassed && curvePassed
      && faceVaryingPassed && quadLightPassed;

  printf("[smoke_test] overall: %s\n", passed ? "PASS" : "FAIL");

  // --- Cleanup ---
  anari::release(d, d);
  anari::unloadLibrary(lib);

  return passed ? 0 : 1;
}
