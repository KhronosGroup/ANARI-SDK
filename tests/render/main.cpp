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

static const std::vector<std::string> g_categories = []() {
  auto v = anari::scenes::getAvailableSceneCategories();
  auto fileBased = [](const std::string &c) { return c == "file"; };
  v.erase(std::remove_if(v.begin(), v.end(), fileBased), v.end());
  std::sort(v.begin(), v.end());
  return v;
}();

static const std::vector<std::vector<std::string>> g_scenes = []() {
  std::vector<std::vector<std::string>> v;
  for (auto &c : g_categories) {
    auto names = anari::scenes::getAvailableSceneNames(c.c_str());
    std::sort(names.begin(), names.end());
    v.push_back(names);
  }
  return v;
}();

std::string g_category;
std::string g_scene;

anari::math::uint2 g_frameSize(1024, 768);
int g_numPixelSamples = 1;
std::string g_libraryType = "environment";
std::string g_deviceType = "default";
std::string g_rendererType = "default";
bool g_enableDebug = false;
bool g_verbose = false;

anari::Library g_library = nullptr;
anari::Device g_device = nullptr;

anari::Library g_debugLibrary = nullptr;

// Helper functions ///////////////////////////////////////////////////////////

static void statusFunc(const void *userData,
    anari::Device device,
    anari::Object source,
    anari::DataType sourceType,
    anari::StatusSeverity severity,
    anari::StatusCode code,
    const char *message)
{
  const bool verbose = userData ? *(const bool *)userData : false;
  if (severity == ANARI_SEVERITY_FATAL_ERROR) {
    fprintf(stderr, "[FATAL][%p] %s\n", source, message);
  } else if (severity == ANARI_SEVERITY_ERROR) {
    fprintf(stderr, "[ERROR][%p] %s\n", source, message);
  } else if (verbose && severity == ANARI_SEVERITY_WARNING) {
    fprintf(stderr, "[WARN ][%p] %s\n", source, message);
  } else if (verbose && severity == ANARI_SEVERITY_PERFORMANCE_WARNING) {
    fprintf(stderr, "[PERF ][%p] %s\n", source, message);
  } else if (verbose && severity == ANARI_SEVERITY_INFO) {
    fprintf(stderr, "[INFO ][%p] %s\n", source, message);
  } else if (verbose && severity == ANARI_SEVERITY_DEBUG) {
    fprintf(stderr, "[DEBUG][%p] %s\n", source, message);
  }
}

static void initializeANARI()
{
  g_library = anari::loadLibrary(g_libraryType.c_str(), statusFunc, &g_verbose);
  if (g_enableDebug)
    g_debugLibrary = anari::loadLibrary("debug", statusFunc, &g_verbose);

  if (!g_library) {
    printf("############ WARNING ############\n");
    printf("'%s' library failed to load, falling back to 'helide'\n",
        g_libraryType.c_str());
    printf("#################################\n");
    g_library = anari::loadLibrary("helide", statusFunc, &g_verbose);
  }

  if (!g_library)
    throw std::runtime_error("Failed to load ANARI library");

  ANARIDevice d = anari::newDevice(g_library, g_deviceType.c_str());

  if (!d)
    std::exit(1);

  anari::commitParameters(d, d);

  int version = -1;

  anari::getProperty(d, d, "version", version);

  printf("\ndevice version: %i\n\n", version);

  g_device = d;
}

static void renderScene(
    ANARIDevice d, const std::string &category, const std::string &scene)
{
  auto s = anari::scenes::createScene(d, category.c_str(), scene.c_str());
  anari::scenes::commit(s);

  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  anari::setParameter(
      d, camera, "aspect", (float)g_frameSize.x / (float)g_frameSize.y);

  auto renderer = anari::newObject<anari::Renderer>(d, g_rendererType.c_str());
  anari::setParameter(d, renderer, "pixelSamples", g_numPixelSamples);
  anari::setParameter(
      d, renderer, "background", anari::math::float4(anari::math::float3(0.1f), 1));
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", g_frameSize);
  anari::setParameter(d, frame, "channel.color", ANARI_UFIXED8_RGBA_SRGB);

  anari::setParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "camera", camera);
  anari::setParameter(d, frame, "world", anari::scenes::getWorld(s));

  anari::commitParameters(d, frame);

  int imgNum = 0;
  auto cameras = anari::scenes::getCameras(s);
  for (auto &cam : cameras) {
    anari::setParameter(d, camera, "position", cam.position);
    anari::setParameter(d, camera, "direction", cam.direction);
    anari::setParameter(d, camera, "up", cam.up);
    anari::commitParameters(d, camera);

    std::string fileName =
        category + '_' + scene + '_' + std::to_string(imgNum++) + ".png";
    printf("rendering '%s/%s'...", category.c_str(), fileName.c_str());

    anari::render(d, frame);
    anari::wait(d, frame);

    printf("done!\n");

    auto fb = anari::map<uint32_t>(d, frame, "channel.color");

    stbi_write_png(fileName.c_str(),
        int(fb.width),
        int(fb.height),
        4,
        fb.data,
        4 * int(fb.width));

    anari::unmap(d, frame, "channel.color");
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
  usage: anariRenderTests [options]

  options:

    --help | -h

        Print this help text

    --library [name] | -l [name]

        Which library to load, which will use the "default" device

        default --> "environment"

    --verbose | -v

        Enable verbose output from the device, otherwise just errors

    --num_samples [N]

        Number of samples to be take for each pixel per-frame,
        relevant to ray tracers

        default --> 1

    --renderer [name]

        Which renderer to use for each frame

        default --> "default"

    --scene [category] [name] | -s [category] [name]

        Which scene to render

        default --> all scenes in all categories will be rendered

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
    } else if (arg == "--scene" || arg == "-s") {
      g_category = argv[++i];
      g_scene = argv[++i];
    } else if (arg == "--image_size") {
      g_frameSize.x = (unsigned)std::strtoul(argv[++i], nullptr, 10);
      g_frameSize.y = (unsigned)std::strtoul(argv[++i], nullptr, 10);
    } else if (arg == "--debug" || arg == "-d") {
      g_enableDebug = true;
    } else if (arg == "--verbose" || arg == "-v") {
      g_verbose = true;
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

  if (g_scene.empty()) {
    int i = 0;
    for (auto &category : g_categories) {
      for (auto &scene : g_scenes[i++])
        renderScene(g_device, category, scene);
    }
  } else {
    renderScene(g_device, g_category, g_scene);
  }

  anari::release(g_device, g_device);
  if (g_enableDebug)
    anari::unloadLibrary(g_debugLibrary);
  anari::unloadLibrary(g_library);

  return 0;
}
