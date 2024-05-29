// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "anari_viewer/Application.h"
#include "anari_viewer/windows/LightsEditor.h"
#include "anari_viewer/windows/SceneSelector.h"
#include "anari_viewer/windows/Viewport.h"
// anari
#include <anari_test_scenes.h>
// std
#include <iostream>

static const bool g_true = true;
static bool g_verbose = false;
static bool g_useDefaultLayout = true;
static bool g_enableDebug = false;
static std::string g_libraryName = "environment";
static anari::Library g_debug = nullptr;
static anari::Device g_device = nullptr;
static const char *g_traceDir = nullptr;

extern const char *getDefaultUILayout();

namespace viewer {

struct AppState
{
  manipulators::Orbit manipulator;
  anari::Device device{nullptr};
};

static void statusFunc(const void *userData,
    ANARIDevice device,
    ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message)
{
  const bool verbose = userData ? *(const bool *)userData : false;
  if (severity == ANARI_SEVERITY_FATAL_ERROR) {
    fprintf(stderr, "[FATAL][%p] %s\n", source, message);
    std::exit(1);
  } else if (severity == ANARI_SEVERITY_ERROR) {
    fprintf(stderr, "[ERROR][%p] %s\n", source, message);
  } else if (severity == ANARI_SEVERITY_WARNING) {
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
  auto library =
      anariLoadLibrary(g_libraryName.c_str(), statusFunc, &g_verbose);
  if (!library)
    throw std::runtime_error("Failed to load ANARI library");

  if (g_enableDebug)
    g_debug = anariLoadLibrary("debug", statusFunc, &g_true);

  anari::Device dev = anariNewDevice(library, "default");

  anari::unloadLibrary(library);

  if (g_enableDebug)
    anari::setParameter(dev, dev, "glDebug", true);

#ifdef USE_GLES2
  anari::setParameter(dev, dev, "glAPI", "OpenGL_ES");
#else
  anari::setParameter(dev, dev, "glAPI", "OpenGL");
#endif

  if (g_enableDebug) {
    anari::Device dbg = anariNewDevice(g_debug, "debug");
    anari::setParameter(dbg, dbg, "wrappedDevice", dev);
    if (g_traceDir) {
      anari::setParameter(dbg, dbg, "traceDir", g_traceDir);
      anari::setParameter(dbg, dbg, "traceMode", "code");
    }
    anari::commitParameters(dbg, dbg);
    anari::release(dev, dev);
    dev = dbg;
  }

  anari::commitParameters(dev, dev);

  g_device = dev;
}

// Application definition /////////////////////////////////////////////////////

struct Application : public anari_viewer::Application
{
  Application() = default;
  ~Application() override = default;

  anari_viewer::WindowArray setupWindows() override
  {
    ui::init();

    // ANARI //

    m_state.device = g_device;

    // ImGui //

    ImGuiIO &io = ImGui::GetIO();
    io.FontGlobalScale = 1.5f;
    io.IniFilename = nullptr;

    if (g_useDefaultLayout)
      ImGui::LoadIniSettingsFromMemory(getDefaultUILayout());

    auto *viewport = new windows::Viewport(g_device, "Viewport");
    viewport->setManipulator(&m_state.manipulator);

    auto *leditor = new windows::LightsEditor(g_device);

    auto *sselector = new windows::SceneSelector();
    sselector->setCallback([=](const char *category, const char *scene) {
      try {
        auto s = anari::scenes::createScene(g_device, category, scene);
        anari::scenes::commit(s);
        auto w = anari::scenes::getWorld(s);
        viewport->setWorld(w, true);
        leditor->setWorlds({w});
        sselector->setScene(s);
      } catch (const std::runtime_error &e) {
        printf("%s\n", e.what());
      }
    });

    anari_viewer::WindowArray windows;
    windows.emplace_back(viewport);
    windows.emplace_back(leditor);
    windows.emplace_back(sselector);

    return windows;
  }

  void uiFrameStart() override
  {
    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("print ImGui ini")) {
          const char *info = ImGui::SaveIniSettingsToMemory();
          printf("%s\n", info);
        }

        ImGui::EndMenu();
      }

      ImGui::EndMainMenuBar();
    }
  }

  void teardown() override
  {
    anari::release(m_state.device, m_state.device);
    ui::shutdown();
  }

 private:
  AppState m_state;
};

} // namespace viewer

///////////////////////////////////////////////////////////////////////////////

static void printUsage()
{
  std::cout << "./anariViewer [{--help|-h}]\n"
            << "   [{--verbose|-v}] [{--debug|-g}]\n"
            << "   [{--library|-l} <ANARI library>]\n"
            << "   [{--trace|-t} <directory>]\n";
}

static void parseCommandLine(int argc, char *argv[])
{
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-v" || arg == "--verbose")
      g_verbose = true;
    if (arg == "--help" || arg == "-h") {
      printUsage();
      std::exit(0);
    } else if (arg == "--noDefaultLayout")
      g_useDefaultLayout = false;
    else if (arg == "-l" || arg == "--library")
      g_libraryName = argv[++i];
    else if (arg == "--debug" || arg == "-g")
      g_enableDebug = true;
    else if (arg == "--trace" || arg == "-t")
      g_traceDir = argv[++i];
  }
}

int main(int argc, char *argv[])
{
  parseCommandLine(argc, argv);
  viewer::initializeANARI();
  if (!g_device)
    return 1;
  viewer::Application app;
  app.run(1920, 1200, "ANARI Demo Viewer");
  return 0;
}
