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

  // --- Cleanup ---
  anari::release(d, frame);

  passed = passed && cylinderPassed && conePassed && curvePassed;

  printf("[smoke_test] overall: %s\n", passed ? "PASS" : "FAIL");

  // --- Cleanup ---
  anari::release(d, d);
  anari::unloadLibrary(lib);

  return passed ? 0 : 1;
}
