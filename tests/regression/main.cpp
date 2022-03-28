// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// anari_test_scenes
#include "anari_test_scenes.h"
// stb_image
#include "stb_image_write.h"
// std
#include <string>
#include <vector>

// Globals ////////////////////////////////////////////////////////////////////

std::vector<std::string> g_scenes = {
    //
    "cornell_box",
    "gravity_spheres_volume",
    "instanced_cubes",
    "textured_cube",
    "random_spheres",
    "cornell_box_directional",
    "cornell_box_point",
    "cornell_box_spot",
    "cornell_box_ring",
    "cornell_box_quad",
    //"cornell_box_hdri",
    "cornell_box_multilight",
    "cornell_box_quad_geom",
    "texture_cube_samplers",
    //"cornell_box_cone_geom",
    //"cornell_box_cylinder_geom"
    //
};

glm::uvec2 g_frameSize(1024, 768);
int g_numPixelSamples = 16;
std::string g_libraryType = "environment";
std::string g_deviceType = "default";
std::string g_rendererType = "default";
bool g_enableDebug = false;

anari::Library g_library = nullptr;
anari::Device g_device = nullptr;

anari::Library g_debugLibrary = nullptr;

// Helper functions ///////////////////////////////////////////////////////////

static void statusFunc(void *userData,
    anari::Device device,
    anari::Object source,
    anari::DataType sourceType,
    anari::StatusSeverity severity,
    anari::StatusCode code,
    const char *message)
{
  (void)userData;
  if (severity == ANARI_SEVERITY_FATAL_ERROR) {
    fprintf(stderr, "[FATAL] %s\n", message);
  } else if (severity == ANARI_SEVERITY_ERROR) {
    fprintf(stderr, "[ERROR] %s\n", message);
  } else if (severity == ANARI_SEVERITY_WARNING) {
    fprintf(stderr, "[WARN ] %s\n", message);
  } else if (severity == ANARI_SEVERITY_PERFORMANCE_WARNING) {
    fprintf(stderr, "[PERF ] %s\n", message);
  } else if (severity == ANARI_SEVERITY_INFO) {
    fprintf(stderr, "[INFO] %s\n", message);
  }
}

static void initializeANARI()
{
  g_library = anari::loadLibrary(g_libraryType.c_str(), statusFunc);
  if (g_enableDebug)
    g_debugLibrary = anari::loadLibrary("debug");

  if (!g_library)
    throw std::runtime_error("Failed to load ANARI library");

  ANARIDevice d = anari::newDevice(g_library, g_deviceType.c_str());

  if (!d)
    std::exit(1);

  anari::commit(d, d);

  int version = -1;

  anari::getProperty(d, d, "version", version);

  printf("\ndevice version: %i\n\n", version);

  g_device = d;
}

static void renderScene(ANARIDevice d, const std::string &scene)
{
  auto s = anari::scenes::createScene(d, scene.c_str());
  anari::scenes::commit(s);

  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  anari::setParameter(
      d, camera, "aspect", g_frameSize.x / (float)g_frameSize.y);

  auto renderer = anari::newObject<anari::Renderer>(d, g_rendererType.c_str());
  anari::setParameter(d, renderer, "pixelSamples", g_numPixelSamples);
  anari::setParameter(
      d, renderer, "backgroundColor", glm::vec4(glm::vec3(0.1f), 1));
  anari::commit(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", g_frameSize);
  anari::setParameter(d, frame, "color", ANARI_UFIXED8_RGBA_SRGB);

  anari::setParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "camera", camera);
  anari::setParameter(d, frame, "world", anari::scenes::getWorld(s));

  anari::commit(d, frame);

  int imgNum = 0;
  auto cameras = anari::scenes::getCameras(s);
  for (auto &cam : cameras) {
    anari::setParameter(d, camera, "position", cam.position);
    anari::setParameter(d, camera, "direction", cam.direction);
    anari::setParameter(d, camera, "up", cam.up);
    anari::commit(d, camera);

    std::string fileName = scene + '_' + std::to_string(imgNum++) + ".png";
    printf("rendering '%s'...", fileName.c_str());

    anari::render(d, frame);
    anari::wait(d, frame);

    printf("done!\n");

    auto *pixels = (uint32_t *)anari::map(d, frame, "color");

    stbi_write_png(fileName.c_str(),
        g_frameSize.x,
        g_frameSize.y,
        4,
        pixels,
        4 * g_frameSize.x);

    anari::unmap(d, frame, "color");
  }

  anari::release(d, camera);
  anari::release(d, frame);
  anari::release(d, renderer);

  anari::scenes::release(s);
}

void printHelp()
{
  printf("%s",
      R"help(
  usage: anari_regression_tests [options]

  options:

    --help | -h

        Print this help text

    --library [name] | -l [name]

        Which library to load, which will use the "default" device

        default --> "environment"

    --num_samples [N]

        Number of samples to be take for each pixel per-frame,
        relevant to ray tracers

        default --> 16

    --renderer [name]

        Which renderer to use for each frame

        default --> "pathtracer"

    --image_size [width] [height]

        Set the size of the images to be rendered

        default --> 1024 768
    --debug | -d

        Enable the debug layer
)help");
}

void parseCommandLine(int argc, const char *argv[])
{
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      printHelp();
      std::exit(0);
    } else if (arg == "--library" || arg == "-l") {
      g_libraryType = argv[++i];
    } else if (arg == "--num_samples") {
      g_numPixelSamples = std::atoi(argv[++i]);
    } else if (arg == "--renderer") {
      g_rendererType = argv[++i];
    } else if (arg == "--image_size") {
      g_frameSize.x = std::atoi(argv[++i]);
      g_frameSize.y = std::atoi(argv[++i]);
    } else if (arg == "--debug" || arg == "-d") {
      g_enableDebug = true;
    }
  }
}

void printConfig()
{
  printf("CONFIGURATION:\n");
  printf("    image size = {%i, %i}\n", g_frameSize.x, g_frameSize.y);
  printf("        device = %s\n", g_deviceType.c_str());
  printf("      renderer = %s\n", g_rendererType.c_str());
  printf(" pixel samples = %i\n", g_numPixelSamples);
  printf("\n");
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int main(int argc, const char *argv[])
{
  stbi_flip_vertically_on_write(1);

  parseCommandLine(argc, argv);
  printConfig();

  initializeANARI();

  for (auto &scene : g_scenes)
    renderScene(g_device, scene);

  anari::release(g_device, g_device);
  if (g_enableDebug)
    anari::unloadLibrary(g_debugLibrary);
  anari::unloadLibrary(g_library);

  return 0;
}
