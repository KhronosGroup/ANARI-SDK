// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "MainWindow.h"
// C++ std
#include <iostream>
#include <limits>

// Global variables
std::string g_libraryType = "environment";
std::string g_deviceType = "default";
std::string g_rendererType = "default";
std::string g_objFile;

ANARILibrary g_library = nullptr;
ANARIDevice g_device = nullptr;
ANARILibrary g_debug = nullptr;
bool g_enableDebug = false;
bool g_verbose = false;
const bool g_true = true;

/******************************************************************/
static std::string pathOf(const std::string &filename)
{
#ifdef _WIN32
  const char path_sep = '\\';
#else
  const char path_sep = '/';
#endif

  size_t pos = filename.find_last_of(path_sep);
  if (pos == std::string::npos)
    return "";
  return filename.substr(0, pos + 1);
}

/******************************************************************/
/* statusFunc(): the callback to use when a status report is made */
void statusFunc(const void *userData,
    ANARIDevice device,
    ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message)
{
  bool verbose = *(const bool*)userData;
  if (severity == ANARI_SEVERITY_FATAL_ERROR) {
    fprintf(stderr, "[FATAL] %s\n", message);
  } else if (severity == ANARI_SEVERITY_ERROR) {
    fprintf(stderr, "[ERROR] %s\n", message);
  } else if (severity == ANARI_SEVERITY_WARNING) {
    fprintf(stderr, "[WARN ] %s\n", message);
  }

  if (!verbose)
    return;

  if (severity == ANARI_SEVERITY_PERFORMANCE_WARNING) {
    fprintf(stderr, "[PERF ] %s\n", message);
  } else if (severity == ANARI_SEVERITY_INFO) {
    fprintf(stderr, "[INFO ] %s\n", message);
  } else if (severity == ANARI_SEVERITY_DEBUG) {
    fprintf(stderr, "[DEBUG] %s\n", message);
  }
}


/******************************************************************/
static void initializeANARI()
{
  g_library = anariLoadLibrary(g_libraryType.c_str(), statusFunc, &g_verbose);

  if (g_enableDebug)
    g_debug = anariLoadLibrary("debug", statusFunc, &g_true);

  if (!g_library)
    throw std::runtime_error("Failed to load ANARI library");

  ANARIDevice dev = anariNewDevice(g_library, g_deviceType.c_str());
  if (g_enableDebug) {
    ANARIDevice dbg = anariNewDevice(g_debug, "debug");
    anari::setParameter(dbg, dbg, "wrappedDevice", ANARI_DEVICE, &dev);
    anari::commitParameters(dbg, dbg);
    anari::release(dev, dev);
    dev = dbg;
  }
  if (!dev)
    std::exit(1);

  // Affect the callback settings and OGL parameter
  anari::commitParameters(dev, dev);

  // Save in the global variable
  g_device = dev;
}

/******************************************************************/
void printUsage()
{
  std::cout
      << "./anariViewer [{--help|-h}]\n"
      << "   [{--verbose|-v}] [{--debug|-g}]\n"
      << "   [{--library|-l} <ANARI library>]\n"
      << "   [{--device|-d} <ANARI device>]\n"
      << "   [{--renderer|-r} <ANARI renderer>]\n"
      << "   [.obj intput file]"
      << std::endl;
}

/******************************************************************/
void parseCommandLine(int argc, const char *argv[])
{
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      printUsage();
      std::exit(0);
    } else if (arg == "--library" || arg == "-l") {
      g_libraryType = argv[++i];
    } else if (arg == "--device" || arg == "-d") {
      g_deviceType = argv[++i];
    } else if (arg == "--renderer" || arg == "-r") {
      g_rendererType = argv[++i];
    } else if (arg == "--debug" || arg == "-g") {
      g_enableDebug = true;
    } else if (arg == "--verbose" || arg == "-v") {
      g_verbose = true;
    } else {
      g_objFile = arg;
    }
  }
}

/******************************************************************/
int main(int argc, const char *argv[])
{
  // Check for and parse command line options
  parseCommandLine(argc, argv);

  // Create a display window using GLFW
  auto *window = new MainWindow(glm::ivec2(1024, 768));

  // Setup ANARI library and device
  initializeANARI();

  // Set the opening scene.  When no filename argument is provided, default
  //   to the "random_sphere" scene
  window->setDevice(g_device, g_rendererType);
  if (!g_objFile.empty())
    window->setScene("file_obj", "fileName", g_objFile);
  else
    window->setScene("random_spheres");

  // Repeatedly render the scene
  window->mainLoop();

  // Cleanup when loop exits
  delete window;

  anariRelease(g_device, g_device);

  if (g_enableDebug)
    anariUnloadLibrary(g_debug);

  anariUnloadLibrary(g_library);

  return 0;
}
