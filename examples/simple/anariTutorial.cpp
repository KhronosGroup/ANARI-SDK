// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <array>
// anari
#define ANARI_EXTENSION_UTILITY_IMPL
#include "anari/anari_cpp.hpp"
#include "anari/anari_cpp/ext/std.h"
// stb_image
#include "stb_image_write.h"

using uvec2 = std::array<unsigned int, 2>;
using uvec3 = std::array<unsigned int, 3>;
using vec3 = std::array<float, 3>;
using vec4 = std::array<float, 4>;
using box3 = std::array<vec3, 2>;

static void statusFunc(const void *userData,
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

static void onFrameCompletion(const void *, anari::Device d, anari::Frame f)
{
  printf("anari::Device(%p) finished rendering anari::Frame(%p)!\n", d, f);
}

template <typename T>
static T getPixelValue(uvec2 coord, int width, const T *buf)
{
  return buf[coord[1] * width + coord[0]];
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
  anari::Library lib = anari::loadLibrary("helide", statusFunc);

  anari::Extensions extensions =
      anari::extension::getDeviceExtensionStruct(lib, "default");

  if (!extensions.ANARI_KHR_GEOMETRY_TRIANGLE)
    printf("WARNING: device doesn't support ANARI_KHR_GEOMETRY_TRIANGLE\n");
  if (!extensions.ANARI_KHR_CAMERA_PERSPECTIVE)
    printf("WARNING: device doesn't support ANARI_KHR_CAMERA_PERSPECTIVE\n");
  if (!extensions.ANARI_KHR_MATERIAL_MATTE)
    printf("WARNING: device doesn't support ANARI_KHR_MATERIAL_MATTE\n");
  if (!extensions.ANARI_KHR_FRAME_COMPLETION_CALLBACK) {
    printf(
        "INFO: device doesn't support ANARI_KHR_FRAME_COMPLETION_CALLBACK\n");
  }

  anari::Device d = anari::newDevice(lib, "default");

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
  anari::setParameterArray1D(d, mesh, "vertex.position", vertex, 4);
  anari::setParameterArray1D(d, mesh, "vertex.color", color, 4);
  anari::setParameterArray1D(d, mesh, "primitive.index", index, 2);
  anari::commitParameters(d, mesh);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", "color");
  anari::commitParameters(d, mat);

  // put the mesh into a surface
  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", mesh);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::setParameter(d, surface, "id", 2u);
  anari::commitParameters(d, surface);

  // put the surface directly onto the world
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::setParameter(d, world, "id", 3u);
  anari::release(d, surface);

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
  anari::setParameter(d, renderer, "ambientRadiance", 1.f);
  anari::commitParameters(d, renderer);

  printf("done!\n");

  // create and setup frame
  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", imgSize);
  anari::setParameter(d, frame, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  anari::setParameter(d, frame, "channel.primitiveId", ANARI_UINT32);
  anari::setParameter(d, frame, "channel.objectId", ANARI_UINT32);
  anari::setParameter(d, frame, "channel.instanceId", ANARI_UINT32);

  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "world", world);

  anari::setParameter(d,
      frame,
      "frameCompletionCallback",
      (anari::FrameCompletionCallback)onFrameCompletion);

  anari::commitParameters(d, frame);

  printf("rendering frame to tutorial_cpp.png...\n");

  // render one frame
  anari::render(d, frame);
  anari::wait(d, frame);

  // access frame and write its content as PNG file
  auto fb = anari::map<uint32_t>(d, frame, "channel.color");
  stbi_write_png("tutorial_cpp.png",
      int(fb.width),
      int(fb.height),
      4,
      fb.data,
      4 * int(fb.width));
  anari::unmap(d, frame, "channel.color");

  printf("...done!\n");

  // Check center pixel id buffers
  auto fbPrimId = anari::map<uint32_t>(d, frame, "channel.primitiveId");
  auto fbObjId = anari::map<uint32_t>(d, frame, "channel.objectId");
  auto fbInstId = anari::map<uint32_t>(d, frame, "channel.instanceId");

  uvec2 queryPixel = {imgSize[0] / 2, imgSize[1] / 2};

  printf("checking id buffers @ [%u, %u]:\n", queryPixel[0], queryPixel[1]);

  if (fbPrimId.pixelType == ANARI_UINT32) {
    printf("    primId: %u\n",
        getPixelValue(queryPixel, imgSize[0], fbPrimId.data));
  }
  if (fbObjId.pixelType == ANARI_UINT32) {
    printf("     objId: %u\n",
        getPixelValue(queryPixel, imgSize[0], fbObjId.data));
  }
  if (fbPrimId.pixelType == ANARI_UINT32) {
    printf("    instId: %u\n",
        getPixelValue(queryPixel, imgSize[0], fbInstId.data));
  }

  printf("\ncleaning up objects...");

  // final cleanups
  anari::release(d, frame);
  anari::release(d, d);
  anari::unloadLibrary(lib);

  printf("done!\n");

  return 0;
}
