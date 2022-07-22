// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <array>
// anari
#define ANARI_FEATURE_UTILITY_IMPL
#include "anari/anari_cpp.hpp"
#include "anari/anari_cpp/ext/std.h"
// stb_image
#include "stb_image_write.h"

const char *g_libraryName = "environment";

using uvec2 = std::array<unsigned int, 2>;
using uvec3 = std::array<unsigned int, 3>;
using vec3 = std::array<float, 3>;
using vec4 = std::array<float, 4>;
using box3 = std::array<vec3, 2>;

void statusFunc(const void *userData,
    ANARIDevice device,
    ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message)
{
  (void)userData;
  (void)device;
  (void)source;
  (void)sourceType;
  (void)code;
  if (severity == ANARI_SEVERITY_FATAL_ERROR) {
    fprintf(stderr, "[FATAL] %s\n", message);
  } else if (severity == ANARI_SEVERITY_ERROR) {
    fprintf(stderr, "[ERROR] %s\n", message);
  } else if (severity == ANARI_SEVERITY_WARNING) {
    fprintf(stderr, "[WARN ] %s\n", message);
  } else if (severity == ANARI_SEVERITY_PERFORMANCE_WARNING) {
    fprintf(stderr, "[PERF ] %s\n", message);
  } else if (severity == ANARI_SEVERITY_INFO) {
    fprintf(stderr, "[INFO ] %s\n", message);
  } else if (severity == ANARI_SEVERITY_DEBUG) {
    fprintf(stderr, "[DEBUG] %s\n", message);
  }
}

int main(int argc, const char **argv)
{
  (void)argc;
  (void)argv;
  stbi_flip_vertically_on_write(1);

  // image size
  uvec2 imgSize = {1024 /*width*/, 768 /*height*/};

  // camera
  vec3 cam_pos = {0.f, 0.f, 0.f};
  vec3 cam_up = {0.f, 1.f, 0.f};
  vec3 cam_view = {0.1f, 0.f, 1.f};

  // triangle mesh array
  vec3 vertex[] = {{-1.0f, -1.0f, 3.0f},
      {-1.0f, 1.0f, 3.0f},
      {1.0f, -1.0f, 3.0f},
      {0.1f, 0.1f, 0.3f}};
  vec4 color[] = {{0.9f, 0.5f, 0.5f, 1.0f},
      {0.8f, 0.8f, 0.8f, 1.0f},
      {0.8f, 0.8f, 0.8f, 1.0f},
      {0.5f, 0.9f, 0.5f, 1.0f}};
  uvec3 index[] = {{0, 1, 2}, {1, 2, 3}};

  printf("initialize ANARI...");
  anari::Library m_debug = anari::loadLibrary("debug", statusFunc);

  anari::Library lib = anari::loadLibrary(g_libraryName, statusFunc);

  anari::Features features = anari::feature::getObjectFeatures(
      lib, "default", "default", ANARI_DEVICE);

  if (!features.ANARI_KHR_GEOMETRY_TRIANGLE)
    printf("WARNING: device doesn't support ANARI_KHR_GEOMETRY_TRIANGLE\n");
  if (!features.ANARI_KHR_CAMERA_PERSPECTIVE)
    printf("WARNING: device doesn't support ANARI_KHR_CAMERA_PERSPECTIVE\n");
  if (!features.ANARI_KHR_LIGHT_DIRECTIONAL)
    printf("WARNING: device doesn't support ANARI_KHR_LIGHT_DIRECTIONAL\n");
  if (!features.ANARI_KHR_MATERIAL_MATTE)
    printf("WARNING: device doesn't support ANARI_KHR_MATERIAL_MATTE\n");

  ANARIDevice w = anariNewDevice(lib, "default");

  ANARIDevice d = anariNewDevice(m_debug, "debug");
  anari::setParameter(d, d, "wrappedDevice", w);
  anari::setParameter(d, d, "traceMode", "code");
  anari::setParameter(d, d, "traceDir", "trace");
  anari::commitParameters(d, d);

  if (!d) {
    printf("\n\nERROR: could not load default device in library %s\n",
        g_libraryName);
    return 1;
  }

  printf("done!\n");
  printf("setting up camera...");

  // create and setup camera
  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  anari::setParameter(
      d, camera, "aspect", (float)imgSize[0] / (float)imgSize[1]);
  anari::setParameter(d, camera, "position", cam_pos);
  anari::setParameter(d, camera, "direction", cam_view);
  anari::setParameter(d, camera, "up", cam_up);
  anari::commitParameters(
      d, camera); // commit objects to indicate setting parameters is done

  printf("done!\n");
  printf("setting up scene...");

  // The world to be populated with renderable objects
  auto world = anari::newObject<anari::World>(d);

  // create and setup surface and mesh
  auto mesh = anari::newObject<anari::Geometry>(d, "triangle");

  anari::setAndReleaseParameter(
      d, mesh, "vertex.position", anari::newArray1D(d, vertex, 4));

  anari::setAndReleaseParameter(
      d, mesh, "vertex.color", anari::newArray1D(d, color, 4));

  anari::setAndReleaseParameter(
      d, mesh, "primitive.index", anari::newArray1D(d, index, 2));

  anari::commitParameters(d, mesh);

  auto mat = anari::newObject<anari::Material>(d, "matte");

  // put the mesh into a surface
  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", mesh);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);

  // put the surface directly onto the world
  anari::setAndReleaseParameter(
      d, world, "surface", anari::newArray1D(d, &surface));
  anari::release(d, surface);

  // create and setup light for Ambient Occlusion
  auto light = anari::newObject<anari::Light>(d, "directional");
  anari::commitParameters(d, light);
  anari::setAndReleaseParameter(
      d, world, "light", anari::newArray1D(d, &light));
  anari::release(d, light);

  anari::commitParameters(d, world);

  printf("done!\n");

  // print out world bounds
  box3 worldBounds;
  if (anari::getProperty(d, world, "bounds", worldBounds, ANARI_WAIT)) {
    printf("\nworld bounds: ({%f, %f, %f}, {%f, %f, %f}\n\n",
        worldBounds[0][0],
        worldBounds[0][1],
        worldBounds[0][2],
        worldBounds[1][0],
        worldBounds[1][1],
        worldBounds[1][2]);
  } else {
    printf("\nworld bounds not returned\n\n");
  }

  printf("setting up renderer...");

  // create renderer
  auto renderer = anari::newObject<anari::Renderer>(d, "default");
  // objects can be named for easier identification in debug output etc.
  anari::setParameter(d, renderer, "name", "MainRenderer");

  printf("done!\n");

  // complete setup of renderer
  vec4 bgColor = {1.f, 1.f, 1.f, 1.f};
  anari::setParameter(d, renderer, "backgroundColor", bgColor); // white
  anari::commitParameters(d, renderer);

  // create and setup frame
  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", imgSize);
  anari::setParameter(d, frame, "color", ANARI_UFIXED8_RGBA_SRGB);

  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "world", world);

  anari::commitParameters(d, frame);

  printf("rendering initial frame to firstFrame.png...");

  // render one frame
  anari::render(d, frame);
  anari::wait(d, frame);

  // access frame and write its content as PNG file
  auto fb = anari::map<uint32_t>(d, frame, "color");
  stbi_write_png("firstFrame.png",
      int(fb.width),
      int(fb.height),
      4,
      fb.data,
      4 * int (fb.width));
  anari::unmap(d, frame, "color");

  printf("done!\n");
  printf("rendering 10 accumulated frames to accumulatedFrame.png...");

  // render 10 more frames, which are accumulated to result in a better
  // converged image
  for (int frames = 0; frames < 10; frames++) {
    anari::render(d, frame);
    anari::wait(d, frame);
  }

  fb = anari::map<uint32_t>(d, frame, "color");
  stbi_write_png("accumulatedFrame.png",
      int(fb.width),
      int(fb.height),
      4,
      fb.data,
      4 * int (fb.width));
  anari::unmap(d, frame, "color");

  printf("done!\n");
  printf("\ncleaning up objects...");

  // final cleanups
  anari::release(d, frame);
  anari::release(d, d);
  anari::unloadLibrary(lib);

  printf("done!\n");

  return 0;
}
