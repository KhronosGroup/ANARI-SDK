// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// anariTutorial.c -- C99 version of a basic ANARI rendering
//
// This example create a scene with two triangles colored at the vertices.
//   The ANARI scene is rendered once and saved into the file "firstFrame.ppm".
//   The scene is then enhanced with 10 additional rendering passes and saved
//   into the file "accumulatedFrame.ppm".  For renderers such at the NVGL
//   raster renderer, there is no improvement to the initial rendering, so
//   both images will appear the same.

#ifdef _WIN32
#include <malloc.h>
#else
#include <alloca.h>
#endif
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// anari
#include "anari/anari.h"
#define ANARI_EXTENSION_UTILITY_IMPL
#include "anari/frontend/anari_extension_utility.h"

/******************************************************************/
/* helper function to write out pixel values to a .ppm file */
void writePPM(const char *fileName, ANARIDevice d, ANARIFrame frame)
{
  uint32_t size[2] = {0, 0};
  ANARIDataType type = ANARI_UNKNOWN;
  uint32_t *pixel = (uint32_t *)anariMapFrame(
      d, frame, "channel.color", &size[0], &size[1], &type);

  if (type != ANARI_UFIXED8_RGBA_SRGB) {
    printf("Incorrectly returned color buffer pixel type, image not saved.\n");
    return;
  }

  FILE *file = fopen(fileName, "wb");
  if (!file) {
    fprintf(stderr, "fopen('%s', 'wb') failed: %d", fileName, errno);
    return;
  }
  fprintf(file, "P6\n%i %i\n255\n", size[0], size[1]);
  unsigned char *out = (unsigned char *)malloc((size_t)(3 * size[0]));
  for (int y = 0; y < size[1]; y++) {
    const unsigned char *in =
        (const unsigned char *)&pixel[(size[1] - 1 - y) * size[0]];
    for (int x = 0; x < size[0]; x++) {
      out[3 * x + 0] = in[4 * x + 0];
      out[3 * x + 1] = in[4 * x + 1];
      out[3 * x + 2] = in[4 * x + 2];
    }
    fwrite(out, (size_t)(3 * size[0]), sizeof(char), file);
  }
  fprintf(file, "\n");
  fclose(file);
  free(out);

  anariUnmapFrame(d, frame, "channel.color");
}

/******************************************************************/
/* errorFunc(): the callback to use when an error is encountered */
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

/******************************************************************/
int main(int argc, const char **argv)
{
  (void)argc;
  (void)argv;
  // image size
  unsigned int imgSize[2] = {1024 /*width*/, 768 /*height*/};

  // clang-format off
  // camera
  float cam_pos[] =  {0.0f, 0.0f, 0.0f};
  float cam_up[] =   {0.0f, 1.0f, 0.0f};	// Y-up
  float cam_view[] = {0.1f, 0.0f, 1.0f};

  // triangle mesh data
  float vertex[] = {
      -1.0f, -1.0f, 3.0f,
      -1.0f,  1.0f, 3.0f,
       1.0f, -1.0f, 3.0f,
       0.1f,  0.1f, 0.3f
  };
  float color[] = {
      0.9f, 0.5f, 0.5f, 1.0f,			// red
      0.8f, 0.8f, 0.8f, 1.0f,			// 80% gray
      0.8f, 0.8f, 0.8f, 1.0f,			// 80% gray
      0.5f, 0.9f, 0.5f, 1.0f			// green
  };
  int32_t index[] = {
      0, 1, 2,					// triangle-1
      1, 2, 3					// triangle-2
  };
  // clang-format on

  printf("initialize ANARI...");

  // Use the 'helide' library here, this is where the impl(s) come from
  ANARILibrary lib = anariLoadLibrary("helide", statusFunc, NULL);

  // query available devices
  const char **devices = anariGetDeviceSubtypes(lib);
  if (!devices) {
    puts("No devices anounced.");
  } else {
    puts("Available devices:");
    for (const char **d = devices; *d != NULL; d++)
      printf("  - %s\n", *d);
  }

  // populate a set of extension variables (this is a utility and not part of the
  // core api)
  ANARIExtensions extensions;
  if (anariGetDeviceExtensionStruct(&extensions, lib, "default")) {
    printf("WARNING: library didn't return extension list\n");
  }

  if (!extensions.ANARI_KHR_GEOMETRY_TRIANGLE)
    printf("WARNING: device doesn't support ANARI_KHR_GEOMETRY_TRIANGLE\n");
  if (!extensions.ANARI_KHR_CAMERA_PERSPECTIVE)
    printf("WARNING: device doesn't support ANARI_KHR_CAMERA_PERSPECTIVE\n");
  if (!extensions.ANARI_KHR_LIGHT_DIRECTIONAL)
    printf("WARNING: device doesn't support ANARI_KHR_LIGHT_DIRECTIONAL\n");
  if (!extensions.ANARI_KHR_MATERIAL_MATTE)
    printf("WARNING: device doesn't support ANARI_KHR_MATERIAL_MATTE\n");

  // the remaining queries have to use a device
  ANARIDevice dev = anariNewDevice(lib, "default");

  // query available renderers
  const char **renderers = anariGetObjectSubtypes(dev, ANARI_RENDERER);
  if (!renderers) {
    puts("No renderers available!");
    return 1;
  }

  puts("Available renderers:");
  for (const char **r = renderers; *r != NULL; r++)
    printf("  - %s\n", *r);

  // inspect default renderer parameters
  const ANARIParameter *rendererParams = anariGetObjectInfo(
      dev, ANARI_RENDERER, "default", "parameter", ANARI_PARAMETER_LIST);

  if (!rendererParams) {
    puts("Default renderer has no parameters.");
  } else {
    puts("Parameters of default renderer:");
    for (const ANARIParameter *p = rendererParams; p->name != NULL; p++) {
      const char *desc = anariGetParameterInfo(dev,
          ANARI_RENDERER,
          "default",
          p->name,
          p->type,
          "description",
          ANARI_STRING);
      const int *required = anariGetParameterInfo(dev,
          ANARI_RENDERER,
          "default",
          p->name,
          p->type,
          "required",
          ANARI_BOOL);
      printf("  - [%d] %s, %s: %s\n",
          p->type,
          p->name,
          required && *required ? "required" : "optional",
          desc);
    }
  }

  if (!dev) {
    printf("\n\nERROR: could not load default device in example library\n");
    return 1;
  }

  // commit device
  anariCommitParameters(dev, dev);

  printf("done!\n");
  printf("setting up camera...");

  // create and setup camera
  ANARICamera camera = anariNewCamera(dev, "perspective");
  float aspect = (float)imgSize[0] / (float)imgSize[1];
  anariSetParameter(dev, camera, "aspect", ANARI_FLOAT32, &aspect);
  anariSetParameter(dev, camera, "position", ANARI_FLOAT32_VEC3, cam_pos);
  anariSetParameter(dev, camera, "direction", ANARI_FLOAT32_VEC3, cam_view);
  anariSetParameter(dev, camera, "up", ANARI_FLOAT32_VEC3, cam_up);
  anariCommitParameters(
      dev, camera); // commit each object to indicate mods are done

  printf("done!\n");
  printf("setting up scene...");

  // The world to be populated with renderable objects
  ANARIWorld world = anariNewWorld(dev);

  // create and setup surface and mesh
  //   A mesh requires an index, plus arrays of: locations & colors
  ANARIGeometry mesh = anariNewGeometry(dev, "triangle");

  // Set the vertex locations
  ANARIArray1D array =
      anariNewArray1D(dev, vertex, 0, 0, ANARI_FLOAT32_VEC3, 4);
  anariCommitParameters(dev, array);
  anariSetParameter(dev, mesh, "vertex.position", ANARI_ARRAY1D, &array);
  anariRelease(dev, array); // we are done using this handle

  // Set the vertex colors
  array = anariNewArray1D(dev, color, 0, 0, ANARI_FLOAT32_VEC4, 4);
  anariCommitParameters(dev, array);
  anariSetParameter(dev, mesh, "vertex.color", ANARI_ARRAY1D, &array);
  anariRelease(dev, array);

  // Set the index
  array = anariNewArray1D(dev, index, 0, 0, ANARI_UINT32_VEC3, 2);
  anariCommitParameters(dev, array);
  anariSetParameter(dev, mesh, "primitive.index", ANARI_ARRAY1D, &array);
  anariRelease(dev, array);

  // Affect all the mesh values
  anariCommitParameters(dev, mesh);

  // Set the material rendering parameters
  ANARIMaterial mat = anariNewMaterial(dev, "matte");
  anariSetParameter(dev, mat, "color", ANARI_STRING, "color");
  anariCommitParameters(dev, mat);

  // put the mesh into a surface
  ANARISurface surface = anariNewSurface(dev);
  anariSetParameter(dev, surface, "geometry", ANARI_GEOMETRY, &mesh);
  anariSetParameter(dev, surface, "material", ANARI_MATERIAL, &mat);
  anariCommitParameters(dev, surface);
  anariRelease(dev, mesh);
  anariRelease(dev, mat);

  // put the surface directly onto the world
  array = anariNewArray1D(dev, &surface, 0, 0, ANARI_SURFACE, 1);
  anariCommitParameters(dev, array);
  anariSetParameter(dev, world, "surface", ANARI_ARRAY1D, &array);
  anariRelease(dev, surface);
  anariRelease(dev, array);

  // create and setup light for Ambient Occlusion
  ANARILight light = anariNewLight(dev, "directional");
  anariCommitParameters(dev, light);
  array = anariNewArray1D(dev, &light, 0, 0, ANARI_LIGHT, 1);
  anariCommitParameters(dev, array);
  anariSetParameter(dev, world, "light", ANARI_ARRAY1D, &array);
  anariRelease(dev, light);
  anariRelease(dev, array);

  anariCommitParameters(dev, world);

  printf("done!\n");

  // print out world bounds
  float worldBounds[6];
  if (anariGetProperty(dev,
          world,
          "bounds",
          ANARI_FLOAT32_BOX3,
          worldBounds,
          sizeof(worldBounds),
          ANARI_WAIT)) {
    printf("\nworld bounds: ({%f, %f, %f}, {%f, %f, %f}\n\n",
        worldBounds[0],
        worldBounds[1],
        worldBounds[2],
        worldBounds[3],
        worldBounds[4],
        worldBounds[5]);
  } else {
    printf("\nworld bounds not returned\n\n");
  }

  printf("setting up renderer...");

  // create renderer
  ANARIRenderer renderer = anariNewRenderer(dev, "default");

  // complete setup of renderer
  float bgColor[4] = {1.f, 1.f, 1.f, 1.f}; // white
  anariSetParameter(dev, renderer, "background", ANARI_FLOAT32_VEC4, bgColor);
  anariCommitParameters(dev, renderer);

  // create and setup frame
  ANARIFrame frame = anariNewFrame(dev);
  anariSetParameter(dev, frame, "size", ANARI_UINT32_VEC2, imgSize);
  ANARIDataType fbFormat = ANARI_UFIXED8_RGBA_SRGB;
  anariSetParameter(dev, frame, "channel.color", ANARI_DATA_TYPE, &fbFormat);

  anariSetParameter(dev, frame, "renderer", ANARI_RENDERER, &renderer);
  anariSetParameter(dev, frame, "camera", ANARI_CAMERA, &camera);
  anariSetParameter(dev, frame, "world", ANARI_WORLD, &world);

  anariCommitParameters(dev, frame);

  printf("rendering initial frame to firstFrame.ppm...");

  // render one frame
  anariRenderFrame(dev, frame);
  anariFrameReady(dev, frame, ANARI_WAIT);

  // access frame and write its content as PNG file
  writePPM("firstFrame.ppm", dev, frame);

  printf("done!\n");
  printf("rendering 10 accumulated frames to accumulatedFrame.ppm...");

  // render 10 more frames, which are accumulated to result in a better
  //   converged image
  for (int frames = 0; frames < 10; frames++) {
    anariRenderFrame(dev, frame);
    anariFrameReady(dev, frame, ANARI_WAIT);
  }

  writePPM("accumulatedFrame.ppm", dev, frame);

  printf("done!\n");
  printf("\ncleaning up objects...");

  // final cleanups
  anariRelease(dev, renderer);
  anariRelease(dev, camera);
  anariRelease(dev, frame);
  anariRelease(dev, world);

  anariRelease(dev, dev);

  anariUnloadLibrary(lib);

  printf("done!\n");

  return 0;
}
