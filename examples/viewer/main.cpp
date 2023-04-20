// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "windows/LightsEditor.h"
#include "windows/Viewport.h"

static bool g_verbose = false;
static bool g_useDefaultLayout = true;
static std::string g_libraryName = "environment";

extern const char *getDefaultUILayout();

namespace viewer {

struct AppState
{
  struct Windows
  {
    windows::Viewport *viewport{nullptr};
  } windows;

  manipulators::Orbit manipulator;

  anari::Library library{nullptr};
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

// Application definition /////////////////////////////////////////////////////

class Application : public match3D::DockingApplication
{
 public:
  Application() = default;
  ~Application() override = default;

  match3D::WindowArray setup() override
  {
    // ANARI //

    m_state.library =
        anari::loadLibrary(g_libraryName.c_str(), statusFunc, &g_verbose);
    m_state.device = anari::newDevice(m_state.library, "default");

    anari::unloadLibrary(m_state.library);

    if (!m_state.device)
      std::exit(1);

    // Scene //

    // auto world = makeWorld(m_state.device, *m_state.scene);
    auto world = anari::newObject<anari::World>(m_state.device);

    // ImGui //

    ImGuiIO &io = ImGui::GetIO();
    io.FontGlobalScale = 1.5f;
    io.IniFilename = nullptr;

    if (g_useDefaultLayout)
      ImGui::LoadIniSettingsFromMemory(getDefaultUILayout());

    m_state.windows.viewport =
        new windows::Viewport(m_state.library, m_state.device, "Viewport");

    m_state.windows.viewport->setManipulator(&m_state.manipulator);

    m_state.windows.viewport->setWorld(world, true);

    anari::release(m_state.device, world);

    auto *leditor = new windows::LightsEditor({m_state.device});
    leditor->setWorlds({world});

    match3D::WindowArray windows;
    windows.emplace_back(m_state.windows.viewport);
    windows.emplace_back(leditor);

    return windows;
  }

  void buildMainMenuUI() override
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
  }

 private:
  AppState m_state;
};

} // namespace viewer

///////////////////////////////////////////////////////////////////////////////

static void parseCommandLine(int argc, char *argv[])
{
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-v" || arg == "--verbose")
      g_verbose = true;
    else if (arg == "--noDefaultLayout")
      g_useDefaultLayout = false;
    else if (arg == "-l" || arg == "--library")
      g_libraryName = argv[++i];
  }
}

int main(int argc, char *argv[])
{
  parseCommandLine(argc, argv);
  viewer::Application app;
  app.run(1920, 1200, "ANARI Multi-Device Demo");
}
