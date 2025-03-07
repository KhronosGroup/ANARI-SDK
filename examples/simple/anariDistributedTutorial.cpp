// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include <errno.h>
#include <mpi.h>
#include <stdint.h>
#include <stdio.h>
#include <array>

// anari
#define ANARI_EXTENSION_UTILITY_IMPL
#include "anari/anari_cpp.hpp"
#include "anari/anari_cpp/ext/std.h"
// stb_image
#include "stb_image_write.h"

using namespace anari::std_types;

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

int main(int argc, char **argv)
{
  int mpiThreadCapability = 0;
  MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &mpiThreadCapability);
  if (mpiThreadCapability != MPI_THREAD_MULTIPLE
      && mpiThreadCapability != MPI_THREAD_SERIALIZED) {
    fprintf(stderr,
        "MPI runtime must support either thread multiple or thread serialized.\n");
    return 1;
  }

  int mpiRank = 0;
  int mpiWorldSize = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &mpiRank);
  MPI_Comm_size(MPI_COMM_WORLD, &mpiWorldSize);

  // image size
  uvec2 imgSize = {1024 /*width*/, 768 /*height*/};

  // camera
  vec3 cam_pos{(mpiWorldSize + 1.f) / 2.f, 0.5f, -mpiWorldSize * 0.5f};
  vec3 cam_up{0.f, 1.f, 0.f};
  vec3 cam_view{0.f, 0.f, 1.f};

  // all ranks specify the same rendering parameters, with the exception of
  // the data to be rendered, which is distributed among the ranks
  // triangle mesh data
  vec3 vertex[] = {{(float)mpiRank, 0.0f, 3.5f},
      {(float)mpiRank, 1.0f, 3.0f},
      {mpiRank + 1.f, 0.0f, 3.0f},
      {mpiRank + 1.f, 1.0f, 2.5f}};
  vec4 color[] = {{0.0f, 0.0f, (mpiRank + 1.f) / mpiWorldSize, 1.0f},
      {0.0f, 0.0f, (mpiRank + 1.f) / mpiWorldSize, 1.0f},
      {0.0f, 0.0f, (mpiRank + 1.f) / mpiWorldSize, 1.0f},
      {0.0f, 0.0f, (mpiRank + 1.f) / mpiWorldSize, 1.0f}};
  uvec3 index[] = {{0, 1, 2}, {1, 2, 3}};

  anari::Library lib = anari::loadLibrary("environment", statusFunc);

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

  anari::Device d = anari::newDevice(lib, "distributed");

  // create and setup camera
  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  anari::setParameter(
      d, camera, "aspect", (float)imgSize[0] / (float)imgSize[1]);
  anari::setParameter(d, camera, "position", cam_pos);
  anari::setParameter(d, camera, "direction", cam_view);
  anari::setParameter(d, camera, "up", cam_up);
  anari::commitParameters(
      d, camera); // commit objects to indicate setting parameters is done

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
  anari::commitParameters(d, surface);

  // put the surface directly onto the world
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::release(d, surface);
  anari::commitParameters(d, world);

  auto renderer = anari::newObject<anari::Renderer>(d, "default");
  anari::commitParameters(d, renderer);

  // create and setup frame
  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", imgSize);
  anari::setParameter(d, frame, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "world", world);
  anari::setParameter(d,
      frame,
      "frameCompletionCallback",
      (anari::FrameCompletionCallback)onFrameCompletion);
  anari::commitParameters(d, frame);

  // render one frame
  anari::render(d, frame);
  anari::wait(d, frame);

  // on rank 0, access frame and write its content as PNG file
  if (mpiRank == 0) {
    auto fb = anari::map<uint32_t>(d, frame, "channel.color");
    stbi_flip_vertically_on_write(1);
    stbi_write_png("tutorialDistributed.png",
        int(fb.width),
        int(fb.height),
        4,
        fb.data,
        4 * int(fb.width));
    anari::unmap(d, frame, "channel.color");
  }

  // final cleanups
  anari::release(d, frame);
  anari::release(d, d);
  anari::unloadLibrary(lib);

  MPI_Finalize();

  return 0;
}
