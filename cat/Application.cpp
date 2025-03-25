// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "PerformanceMetrics.h"

#include <iostream>
#include "Application.h"

#include "anari_test_scenes.h"
#include "anari_test_scenes/scenes/scene.h"

#include "anari_viewer/Application.h"
#include "anari_viewer/Orbit.h"
#include "anari_viewer/ui_anari.h"
#include "anari_viewer/windows/Viewport.h"

#include "PerformanceMetrics.h"
#include "windows/PerformanceMetricsViewer.h"
#include "windows/SceneSelector.h"

// cli11
#include "CLI/CLI.hpp"

// implot
#include "implot.h"

// kokkos
#if defined(USE_KOKKOS)
#include "Kokkos_Core.hpp"
#include <thread>
#endif

// stb_image
#include "stb_image_write.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

extern const char *getDefaultUILayout();

namespace {

void anariStatusFunc(const void *userData,
    ANARIDevice device,
    ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message)
{
  const bool verbose =
      userData ? *(static_cast<const bool *>(userData)) : false;
  if (severity == ANARI_SEVERITY_FATAL_ERROR) {
    std::cerr << "[FATAL][0x" << std::hex << source << std::dec << "]"
              << message << '\n';
    std::exit(EXIT_FAILURE);
  } else if (severity == ANARI_SEVERITY_ERROR) {
    std::cerr << "[ERROR][0x" << std::hex << source << std::dec << "]"
              << message << '\n';
  } else if (severity == ANARI_SEVERITY_WARNING) {
    std::cerr << "[WARN ][0x" << std::hex << source << std::dec << "]"
              << message << '\n';
  } else if (verbose && severity == ANARI_SEVERITY_PERFORMANCE_WARNING) {
    std::cerr << "[PERF ][0x" << std::hex << source << std::dec << "]"
              << message << '\n';
  } else if (verbose && severity == ANARI_SEVERITY_INFO) {
    std::cerr << "[INFO ][0x" << std::hex << source << std::dec << "]"
              << message << '\n';
  } else if (verbose && severity == ANARI_SEVERITY_DEBUG) {
    std::cerr << "[DEBUG][0x" << std::hex << source << std::dec << "]"
              << message << '\n';
  }
}

template <typename T>
std::string toStringWithPrecision(T value, int n = 1)
{
  std::ostringstream out;
  out.precision(n);
  out << std::fixed << value;
  return out.str();
}

template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
std::string padString(T value, char pad, std::size_t length)
{
  std::ostringstream out;
  out << std::setfill(pad) << std::setw(length) << value;
  return out.str();
}
} // namespace

namespace anari_cat {
struct Application::Impl
{
  anari_viewer::manipulators::Orbit manipulator;
  anari::Device device = nullptr;
  anari::Library library = nullptr;
  
  char **argv = nullptr;
  
  float framesDropped = 0.f;
  float frameTimeStamp = 0.f;
  
  int argc = 0;
  int initCategoryId = 0;
  int initRendererId = 0;
  int initSceneId = 0;
  
  mutable bool frameReady = false;
  mutable float lastFrameTimeMilis = 0.f;
  
  std::ofstream csv;
  
  std::shared_ptr<anari_cat::Recorder> metricsRecorder;
  
  std::size_t framesRendered = 0;
  std::size_t numFramesWidth = 0;
  std::size_t timespanWidth = 0;
  
  std::unique_ptr<CLI::App> app;
  
  std::vector<std::string> deviceNames;
  std::vector<std::string> sceneNames;
  std::vector<std::vector<std::string>> rendererNames;
  
#if defined(USE_KOKKOS)
  unsigned int kokkosNumThreads = std::thread::hardware_concurrency();
#endif
};

Application::Application(int argc, char *argv[])
    : anari_viewer::Application(), m_impl(new Impl{})
{
  //
  // Initialize command line argument parser
  //
  m_impl->app.reset(new CLI::App{"ANARI Capability Analysis Tool"});
  m_impl->app->add_flag("-v,--verbose", m_verbose, "Enable verbose output")
      ->capture_default_str();
  // require atleast one subcommand.
  m_impl->app->require_subcommand();

  //
  // [Subcommand: 'list']
  // The list subcommand can be used to print
  // a list of ANARI devices at runtime and,
  // scenes from the current build of
  // ANARI-SDK.
  // require atleast one kind of items to list.
  //
  auto *listCmd = m_impl->app->add_subcommand("list")->require_subcommand();

  //
  // [Subcommand: 'list' 'devices']
  //
  auto *devicesCmd = listCmd->add_subcommand("devices")->callback(
      [this]() { onList(ListItemType::Devices); });
  // Add options for the `list devices` subcommand
  devicesCmd->add_option("-l,--library", m_library, "ANARI implementation name")
      ->capture_default_str();

  //
  // [Subcommand: 'list' 'renderers']
  //
  auto *renderersCmd = listCmd->add_subcommand("renderers")->callback([this]() {
    onList(ListItemType::Renderers);
  });
  // Add options for the `list renderers` subcommand
  renderersCmd
      ->add_option("-l,--library", m_library, "ANARI implementation name")
      ->capture_default_str();

  //
  // [Subcommand: 'list' 'scenes']
  //
  listCmd->add_subcommand("scenes")->callback(
      [this]() { onList(ListItemType::Scenes); });

  //
  // [Subcommand: 'run']
  // The run subcommand either launches GUI or renders a specified scene
  // non-interactively i.e, without launching a window.
  //
  auto *runCmd =
      m_impl->app->add_subcommand("run")->callback([this] { onRun(); });

  runCmd->add_flag("-a,--animate", m_animate, "Play animation")
      ->capture_default_str();
  runCmd->add_flag("-g,--gui", m_gui, "Enable GUI")->capture_default_str();
  runCmd
      ->add_flag(
          "-o,--orthographic", m_orthographic, "Enable orthographic camera")
      ->capture_default_str();

  runCmd
      ->add_option("-t,--timespan",
          m_timespan,
          " Continuously render frames for the specified timespan (seconds)."
          " Exits the application if the frame timestamp exceeds timespan.")
      ->group("non-interactive-only")
      ->capture_default_str();

  runCmd
      ->add_option("--size",
          m_size,
          "In interactive mode, the entered values correspond to the dimensions for GUI window."
          " In non-interactive mode, the entered values correspond to the dimensions of the frame."
          " Ex: --size 1920 1080")
      ->expected(2) // width height
      ->capture_default_str();

  runCmd->add_option("-d,--device", m_device_id, "Device ID")
      ->capture_default_str();
  runCmd
      ->add_option("-n,--num-frames",
          m_num_frames,
          " Render at least specified number of frames.")
      ->group("non-interactive-only")
      ->capture_default_str();
  runCmd
      ->add_option("-c,--capture-screenshot",
          m_capture_screenshot,
          " Save screenshot every N frames. Uses PNG format.")
      ->group("non-interactive-only")
      ->capture_default_str();

  runCmd->add_option("-l,--library", m_library, "ANARI implementation name")
      ->capture_default_str();
  runCmd
      ->add_option("-m,--log-metrics",
          m_log_metrics,
          "Save performance metric information to a CSV file.")
      ->capture_default_str();
  runCmd->add_option("-r,--renderer", m_renderer, "ANARI renderer subtype")
      ->capture_default_str();
  runCmd->add_option("-s,--scene", m_scene, "Scene name. Ex: <category:scene>")
      ->capture_default_str();

#if defined(USE_KOKKOS)
  //
  // Kokkos command line options.
  //
  runCmd
      ->add_option("--kokkos-num-threads",
          m_impl->kokkosNumThreads,
          "No. of threads for kokkos")
      ->group("Kokkos")
      ->capture_default_str();
#endif

  // take snapshot of commandline args for reuse in `exec`
  m_impl->argc = argc;
  m_impl->argv = new char *[argc];
  for (int i = 0; i < argc; ++i) {
    const auto n = strlen(argv[i]) + 1;
    m_impl->argv[i] = new char[n];
    snprintf(m_impl->argv[i], n, "%s", argv[i]);
  }
}

Application::~Application()
{
#if defined(USE_KOKKOS)
  Kokkos::finalize();
#endif
  if (m_impl->csv.is_open()) {
    m_impl->csv.close();
    const std::filesystem::path absolutePath =
        std::filesystem::absolute(m_log_metrics);
    std::cout << "Saved performance metrics to " << absolutePath << '\n';
  }
  if (m_impl->library) {
    anari::unloadLibrary(m_impl->library);
  }
  for (int i = 0; i < m_impl->argc; ++i) {
    delete m_impl->argv[i];
  }
  delete m_impl->argv;
}

anari_viewer::WindowArray Application::setupWindows()
{
  //
  // Initialize NFD (file dialog)
  //
  anari_viewer::ui::init();

  //
  // Initialize ImPlot.
  // Superclass already initialized ImGui before `setupWindows()
  //
  ImPlot::CreateContext();

  //
  // Initialize ImGUI Font/UI layout
  //
  auto &io = ImGui::GetIO();
  io.FontGlobalScale = 1.5f;
  io.IniFilename = nullptr;
  ImGui::LoadIniSettingsFromMemory(getDefaultUILayout());

  //
  // Create Viewport
  //
  auto *viewport = new anari_viewer::windows::Viewport(m_impl->device,
      /*name=*/"Viewport",
      /*useOrthoCamera=*/m_orthographic,
      /*initRendererId=*/m_impl->initRendererId);
  viewport->setManipulator(&m_impl->manipulator);
  viewport->setViewportFrameReadyCallback(onViewportFrameReady, m_impl.get());

  //
  // Create SceneSelector
  //
  auto *sselector = new anari_cat::windows::SceneSelector(
      /*name=*/"Scene", m_impl->initCategoryId, m_impl->initSceneId, m_animate);
  sselector->setPerformanceRecorder(m_impl->metricsRecorder);
  sselector->setCallback([=](const char *category, const char *scene) {
    try {
      // Start scene construct timer.
      m_impl->metricsRecorder->StartTimer(anari_cat::TIME_SCENE_BUILD);
      auto s = anari::scenes::createScene(m_impl->device, category, scene);
      anari::scenes::commit(s);
      auto w = anari::scenes::getWorld(s);
      viewport->setWorld(w, true);
      sselector->setScene(s);
      // Stop scene construct timer.
      m_impl->metricsRecorder->StopTimer(anari_cat::TIME_SCENE_BUILD);
    } catch (const std::runtime_error &e) {
      std::cerr << e.what() << '\n';
    }
  });

  //
  // Create PerformanceMetricsViewer overlay
  //
  auto *performanceMetricsViewer =
      new anari_cat::windows::PerformanceMetricsViewer(m_impl->metricsRecorder);
  viewport->addOverlay(performanceMetricsViewer);

  anari_viewer::WindowArray windows;
  windows.emplace_back(viewport);
  windows.emplace_back(sselector);
  return windows;
}

void Application::mainLoopStart()
{
  //
  // Start Application observed frame timer.
  //
  m_impl->metricsRecorder->StartTimer(anari_cat::LATENCY_APPLICATION);
}

void Application::uiFrameStart()
{
  //
  // Initialize to track dropped frames.
  // The onViewportFrameReady callback will set it to true.
  //
  m_impl->frameReady = false;

  //
  // Start UI frame timer
  //
  m_impl->metricsRecorder->StartTimer(anari_cat::LATENCY_UI);
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("print ImGui ini")) {
        const char *info = ImGui::SaveIniSettingsToMemory();
        std::cout << info << '\n';
      }

      ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
  }
}

void Application::uiRenderStart()
{
  if (!m_impl->frameReady) {
    //
    // if viewport did not yet signal that the new frame was ready,
    // it means that the graphics layer is about to present a
    // frame that was presented in a previous iteration.
    //
    m_impl->framesDropped++;
  } else {
    // increment rendered frame counter.
    m_impl->framesRendered++;
    // reset frame dropped counter.
    m_impl->framesDropped = 0;
  }
  //
  // Start presentation timer
  //
  m_impl->metricsRecorder->StartTimer(anari_cat::LATENCY_PRESENTATION);
}

void Application::uiRenderEnd()
{
  //
  // Stop presentation timer
  //
  m_impl->metricsRecorder->StopTimer(anari_cat::LATENCY_PRESENTATION);
}

void Application::uiFrameEnd()
{
  //
  // Stop UI frame timer
  //
  m_impl->metricsRecorder->StopTimer(anari_cat::LATENCY_UI);
  m_impl->metricsRecorder->SetMetricsValue(
      anari_cat::DROPPED_FRAMES, m_impl->framesDropped);
}

void Application::mainLoopEnd()
{
  //
  // Stop Application observed frame timer.
  //
  m_impl->metricsRecorder->StopTimer(anari_cat::LATENCY_APPLICATION);
  m_impl->metricsRecorder->SetMetricsValue(
      anari_cat::LATENCY_ANARI_DEVICE, m_impl->lastFrameTimeMilis);
  m_impl->frameTimeStamp += ImGui::GetIO().DeltaTime;
  auto metricData = m_impl->metricsRecorder->Collect(m_impl->frameTimeStamp);
  this->logMetrics(metricData);
}

void Application::teardown()
{
  ImPlot::DestroyContext();
  anari::release(m_impl->device, m_impl->device);
  anari_viewer::ui::shutdown();
}

int Application::exec()
{
  CLI11_PARSE((*m_impl->app), m_impl->argc, m_impl->argv);
  return EXIT_SUCCESS;
}

bool Application::getANARIDevices()
{
  if (m_impl->library == nullptr) {
    m_impl->library =
        anari::loadLibrary(m_library.c_str(), anariStatusFunc, &m_verbose);
  }
  if (m_impl->library) {
    if (const char **devices = anariGetDeviceSubtypes(m_impl->library)) {
      for (std::size_t i = 0; devices[i]; ++i) {
        m_impl->deviceNames.emplace_back(devices[i]);
      }
      return true;
    } else {
      std::cerr << "Failed to retreive devices from ANARI library "
                << m_library;
    }
  } else {
    std::cerr << "Failed to load ANARI library";
  }
  return false;
}

bool Application::getANARIRenderers()
{
  if (this->getANARIDevices()) {
    for (const auto &deviceName : m_impl->deviceNames) {
      m_impl->rendererNames.emplace_back();
      auto &rendererNames = m_impl->rendererNames.back();
      // Create device.
      auto device = anari::newDevice(m_impl->library, deviceName.c_str());
      // Get renderer subtypes
      const char **renderers = anariGetObjectSubtypes(device, ANARI_RENDERER);
      if (renderers != nullptr) {
        for (int i = 0; renderers[i] != nullptr; i++) {
          std::string subtype = renderers[i];
          rendererNames.emplace_back(subtype);
        }
      } else {
        rendererNames.emplace_back("default");
      }
      anari::release(device, device);
    }
    return true;
  }
  return false;
}

bool Application::getANARITestScenes()
{
  //
  // Retreive scenes.
  //
  const auto categories = anari::scenes::getAvailableSceneCategories();
  for (const auto &category : categories) {
    const auto scenes = anari::scenes::getAvailableSceneNames(category.c_str());
    for (const auto &scene : scenes) {
      m_impl->sceneNames.emplace_back(category + ":" + scene);
    }
  }
  return !m_impl->sceneNames.empty();
}

void Application::onList(ListItemType type)
{
  switch (type) {
  case Devices:
    if (this->getANARIDevices()) {
      std::cout << "Available devices:\n";
      for (std::size_t i = 0; i < m_impl->deviceNames.size(); ++i) {
        std::cout << i << "\t" << m_impl->deviceNames[i] << '\n';
      }
    } else {
      std::cerr << "There are no ANARI devices!\n";
    }
    break;
  case Renderers:
    if (this->getANARIRenderers()) {
      if (m_impl->rendererNames.size() > m_device_id) {
        std::cout << "Available renderers on device \'"
                  << m_impl->deviceNames[m_device_id] << "\'\n";
        const auto &m_renderers = m_impl->rendererNames[m_device_id];
        for (std::size_t i = 0; i < m_renderers.size(); ++i) {
          std::cout << i << "\t" << m_renderers[i] << '\n';
        }
      } else {
        std::cout << "There are no renderers on device \'"
                  << m_impl->deviceNames[m_device_id] << "\'!\n";
      }
    }
    break;
  case Scenes:
    if (this->getANARITestScenes()) {
      std::cout << "Available scenes:\n";
      for (std::size_t i = 0; i < m_impl->sceneNames.size(); ++i) {
        std::cout << i << "\t" << m_impl->sceneNames[i] << '\n';
      }
    } else {
      std::cerr << "There are no test scenes!\n";
    }
    break;
  }
}

void Application::onRun()
{
  if (!this->getANARIRenderers()) {
    return;
  }

  if (!this->getANARITestScenes()) {
    return;
  }

  if (m_device_id >= m_impl->deviceNames.size()) {
    std::cerr << "Invalid device ID!" << " ID must be within [0, "
              << m_impl->deviceNames.size() << ")\n";
    return;
  }

  //
  // Create device.
  //
  const char *deviceName = m_impl->deviceNames[m_device_id].c_str();
  m_impl->device = anari::newDevice(m_impl->library, deviceName);

#ifdef USE_GLES2
  anari::setParameter(m_impl->device, m_impl->device, "glAPI", "OpenGL_ES");
#else
  anari::setParameter(m_impl->device, m_impl->device, "glAPI", "OpenGL");
#endif
  anari::commitParameters(m_impl->device, m_impl->device);

  m_impl->initRendererId = 0;
  m_impl->initCategoryId = 0;
  m_impl->initSceneId = 0;
  //
  // Find the renderer ID to initialize the Viewport.
  //
  {
    const auto rendererNames = m_impl->rendererNames[m_device_id];
    auto rendererIt =
        std::find(rendererNames.begin(), rendererNames.end(), m_renderer);
    if (rendererIt == rendererNames.end()) {
      std::cerr << "Invalid renderer \'" << m_renderer
                << "\'! Fallback to \'default\'.";
    } else {
      m_impl->initRendererId =
          static_cast<int>(std::distance(rendererNames.begin(), rendererIt));
    }
  }
  //
  // Find the category and scene ID to initialize the SceneSelector.
  //
  std::string initCategory, initScene;
  bool foundCategoryScene = false;
  const auto categories = anari::scenes::getAvailableSceneCategories();
  for (const auto &category : categories) {
    const auto scenes = anari::scenes::getAvailableSceneNames(category.c_str());
    m_impl->initSceneId = 0;
    for (const auto &scene : scenes) {
      const auto categoryScene = category + ":" + scene;
      if (categoryScene == m_scene) {
        initCategory = category;
        initScene = scene;
        foundCategoryScene = true;
        break;
      }
      m_impl->initSceneId++;
    }
    if (foundCategoryScene) {
      break;
    }
    m_impl->initCategoryId++;
  }
  if (m_impl->initCategoryId >= categories.size()) {
    std::cerr << "Scene " << m_scene
              << " is invalid. Please run the 'list scenes' subcommand.\n";
    return;
  }

  //
  // Required by some scenes which accelerate builds and frequent updates.
  //
#if defined(USE_KOKKOS)
  Kokkos::InitializationSettings kokkosSettings;
  kokkosSettings.set_num_threads(static_cast<int>(m_impl->kokkosNumThreads));
  Kokkos::initialize(kokkosSettings);
#endif

  m_impl->csv.open(
      m_log_metrics, std::ios::binary | std::ios::out | std::ios::trunc);
  if (m_gui) {
    //
    // Create metrics recorder with a scrolling buffer (used to display a
    // scrolling plot in UI)
    //
    m_impl->metricsRecorder = std::make_shared<anari_cat::Recorder>();
    // print header
    // clang-format off
    m_impl->csv << "Frame,"
      << GetLabel(TIMESTAMP) << '(' << GetUnits(TIMESTAMP) << "),"
      << GetLabel(LATENCY_ANARI_DEVICE) << '(' << GetUnits(LATENCY_ANARI_DEVICE) << "),"
      << GetLabel(DROPPED_FRAMES) << '(' << GetUnits(DROPPED_FRAMES) << "),"
      << GetLabel(LATENCY_PRESENTATION) << '(' << GetUnits(LATENCY_PRESENTATION) << "),"
      << GetLabel(LATENCY_APPLICATION) << '(' << GetUnits(LATENCY_APPLICATION) << "),"
      << GetLabel(LATENCY_UI) << '(' << GetUnits(LATENCY_UI) << "),"
      << GetLabel(TIME_SCENE_UPDATE) << '(' << GetUnits(TIME_SCENE_UPDATE) << ")"
      << '\n';
    // clang-format on
    //
    // Start GUI.
    //
    const auto description = m_impl->app->get_description();
    this->run(m_size[0], m_size[1], description.c_str());
  } else {
    m_impl->numFramesWidth = std::to_string(m_num_frames).size();
    m_impl->timespanWidth = toStringWithPrecision(m_timespan, /*n=*/1).size();
    //
    // Create metrics recorder with an indefinitely growing buffer.
    //
    m_impl->metricsRecorder = std::make_shared<anari_cat::Recorder>();
    // print header
    // clang-format off
    m_impl->csv << "Frame," << GetLabel(TIMESTAMP) << '(' << GetUnits(TIMESTAMP) << "),"
                << GetLabel(LATENCY_ANARI_DEVICE) << '(' << GetUnits(LATENCY_ANARI_DEVICE) << "),"
                << GetLabel(TIME_SCENE_UPDATE) <<  '(' << GetUnits(TIME_SCENE_UPDATE) << ")\n";
    // clang-format on
    //
    // Create frame.
    //
    auto frame = anari::newObject<anari::Frame>(m_impl->device);
    //
    // Create renderer.
    //
    auto renderer =
        anari::newObject<anari::Renderer>(m_impl->device, m_renderer.c_str());
    //
    // Create camera.
    //
    auto camera = anari::newObject<anari::Camera>(
        m_impl->device, (m_orthographic ? "orthographic" : "perspective"));
    m_impl->metricsRecorder->SetComputeStatistics(false);
    //
    // Build scene.
    //
    m_impl->metricsRecorder->StartTimer(anari_cat::TIME_SCENE_BUILD);
    auto scene = anari::scenes::createScene(
        m_impl->device, initCategory.c_str(), initScene.c_str());
    anari::scenes::commit(scene);
    auto world = anari::scenes::getWorld(scene);
    anari::commitParameters(m_impl->device, world);
    m_impl->metricsRecorder->StopTimer(anari_cat::TIME_SCENE_BUILD);
    //
    // Setup camera view frustum.
    //
    anari::scenes::box3 bounds;
    if (!anari::getProperty(
            m_impl->device, world, "bounds", bounds, ANARI_WAIT)) {
      std::cerr
          << "WARNING: bounds not returned by the device! Using unit cube.\n";
    }
    m_impl->manipulator.setConfig(
        /*at=*/0.5f * (bounds[0] + bounds[1]),
        /*dist=*/1.25f * linalg::length(bounds[1] - bounds[0]),
        /*azel=*/{0.f, 0.f});
    anari::setParameter(
        m_impl->device, camera, "direction", m_impl->manipulator.dir());
    anari::setParameter(m_impl->device, camera, "up", m_impl->manipulator.up());
    if (m_orthographic) {
      anari::setParameter(m_impl->device,
          camera,
          "position",
          m_impl->manipulator.eye_FixedDistance());
      anari::setParameter(m_impl->device,
          camera,
          "height",
          m_impl->manipulator.distance() * 0.75f);
    } else {
      anari::setParameter(
          m_impl->device, camera, "position", m_impl->manipulator.eye());
    }
    anari::setParameter(
        m_impl->device, camera, "aspect", m_size[0] / float(m_size[1]));
    anari::commitParameters(m_impl->device, camera);
    //
    // Setup frame
    //
    anari::setParameter(
        m_impl->device, frame, "size", anari::math::uint2{m_size.data()});
    anari::setParameter(
        m_impl->device, frame, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
    anari::setParameter(m_impl->device, frame, "accumulation", true);
    anari::setParameter(m_impl->device, frame, "world", world);
    anari::setParameter(m_impl->device, frame, "camera", camera);
    anari::setParameter(m_impl->device, frame, "renderer", renderer);
    anari::commitParameters(m_impl->device, frame);
    //
    // Start non-interactive main loop.
    //
    std::cout << std::setw(m_impl->numFramesWidth) << "0/" << m_num_frames
              << " frames " << std::setw(m_impl->timespanWidth) << "0s/"
              << m_timespan << "s elapsed";

    Timer timestampTimer;
    timestampTimer.start();
    std::array<MetricStatistics, MetricType::COUNT> statistics = {};
    while (m_impl->frameTimeStamp < m_timespan) {
      if (m_animate) {
        //
        // Start scene update timer.
        //
        m_impl->metricsRecorder->StartTimer(anari_cat::TIME_SCENE_UPDATE);
        anari::scenes::computeNextFrame(scene);
        //
        // Stop scene update timer.
        //
        m_impl->metricsRecorder->StopTimer(anari_cat::TIME_SCENE_UPDATE);
      }
      //
      // Render scene into a ANARI frame.
      //
      anari::render(m_impl->device, frame);
      //
      // Wait indefinitely for ANARI to finish rendering frame.
      //
      anari::wait(m_impl->device, frame);
      //
      // Record time taken by the device.
      //
      float duration = 0.f;
      anari::getProperty(m_impl->device, frame, "duration", duration);
      m_impl->metricsRecorder->SetMetricsValue(
          anari_cat::LATENCY_ANARI_DEVICE, duration * 1000.0f);
      //
      // Collect metrics.
      //
      m_impl->frameTimeStamp = timestampTimer.secondsElapsed();
      m_impl->framesRendered++;
      auto metricData =
          m_impl->metricsRecorder->Collect(m_impl->frameTimeStamp);
      this->logMetrics(metricData);
      // update statistics.
      for (int type = 0; type < MetricType::COUNT; ++type) {
        const auto value = metricData[type];
        statistics[type].Maximum =
            value > statistics[type].Maximum ? value : statistics[type].Maximum;
        statistics[type].Minimum =
            value < statistics[type].Minimum ? value : statistics[type].Minimum;
        statistics[type].Mean += value;
      }

      if (m_capture_screenshot > 0) {
        if ((m_impl->framesRendered) % m_capture_screenshot == 0) {
          //
          // Take screenshot.
          //
          std::string filename = "frame-"
              + padString(m_impl->framesRendered, '0', m_impl->numFramesWidth)
              + ".png";
          auto fb =
              anari::map<std::uint32_t>(m_impl->device, frame, "channel.color");
          if (fb.data) {
            stbi_flip_vertically_on_write(1);
            stbi_write_png(filename.c_str(),
                fb.width,
                fb.height,
                4,
                fb.data,
                4 * fb.width);
          } else {
            std::cerr << "\nmapped bad frame: " << std::hex << fb.data
                      << std::dec << " | " << fb.width << " x " << fb.height
                      << '\n';
          }
          anari::unmap(m_impl->device, frame, "channel.color");
        }
      }
      this->logProgress();
      //
      // Exit loop when the requested number of frames have been rendered.
      //
      if (m_num_frames > 0) {
        if (m_impl->framesRendered == m_num_frames) {
          break;
        }
      }
    }
    std::cout << std::endl;

    anari::release(m_impl->device, camera);
    anari::release(m_impl->device, world);
    anari::release(m_impl->device, renderer);
    anari::release(m_impl->device, frame);
    anari::release(m_impl->device, m_impl->device);

    // clang-format off
    for (int type = 0; type < MetricType::COUNT; ++type) {
      statistics[type].Mean /= m_impl->framesRendered;
    }
    std::cout << "\n===================\nPerformance Summary\n===================\n"
              << "Total # frames rendered" << '\t' << m_impl->framesRendered
              << '\n'
              << GetLabel(TIME_SCENE_BUILD) << '\t'
              << statistics[TIME_SCENE_BUILD].Mean << GetUnits(LATENCY_ANARI_DEVICE)
              << '\n'
              << GetLabel(LATENCY_ANARI_DEVICE) << "\n\tAvg" << '\t'
              << statistics[LATENCY_ANARI_DEVICE].Mean
              << GetUnits(LATENCY_ANARI_DEVICE) << "\n\tMin" << '\t'
              << statistics[LATENCY_ANARI_DEVICE].Minimum
              << GetUnits(LATENCY_ANARI_DEVICE) << "\n\tMax" << '\t'
              << statistics[LATENCY_ANARI_DEVICE].Maximum 
              << GetUnits(LATENCY_ANARI_DEVICE) << '\n'
              << GetLabel(TIME_SCENE_UPDATE) << "\n\tAvg" << '\t'
              << statistics[TIME_SCENE_UPDATE].Mean
              << GetUnits(TIME_SCENE_UPDATE) << "\n\tMin" << '\t'
              << statistics[TIME_SCENE_UPDATE].Minimum
              << GetUnits(TIME_SCENE_UPDATE) << "\n\tMax" << '\t'
              << statistics[TIME_SCENE_UPDATE].Maximum
              << GetUnits(TIME_SCENE_UPDATE) << '\n';
    // clang-format on
  }
}

void Application::logMetrics(const ScrollingBuffer::ElementT &metricData)
{
  if (m_gui) {
    m_impl->csv << m_impl->framesRendered << ',' << metricData[TIMESTAMP] << ','
                << metricData[LATENCY_ANARI_DEVICE] << ','
                << metricData[DROPPED_FRAMES] << ','
                << metricData[LATENCY_PRESENTATION] << ','
                << metricData[LATENCY_APPLICATION] << ','
                << metricData[LATENCY_UI] << ','
                << metricData[TIME_SCENE_UPDATE] << '\n';
  } else {
    m_impl->csv << m_impl->framesRendered << ',' << metricData[TIMESTAMP] << ','
                << metricData[LATENCY_ANARI_DEVICE] << ','
                << metricData[TIME_SCENE_UPDATE] << '\n';
  }
}

void Application::logProgress()
{
  std::cout << "\r" << std::setw(m_impl->numFramesWidth)
            << m_impl->framesRendered;
  if (m_num_frames) {
    std::cout << "/" << m_num_frames;
  }
  std::cout << " frames ";
  std::cout << std::fixed << std::setw(m_impl->timespanWidth)
            << std::setprecision(1) << m_impl->frameTimeStamp << "s/"
            << m_timespan << "s elapsed";
  std::cout.flush();
}

void Application::onViewportFrameReady(const void *userPtr,
    const anari_viewer::windows::Viewport *viewport,
    const float duration)
{
  if (const auto *selfImpl = static_cast<const Application::Impl *>(userPtr)) {
    selfImpl->lastFrameTimeMilis = duration * 1000;
    selfImpl->frameReady = true;
  } else {
    std::cerr << "OnViewportFrameReady: UserPtr is null!\n";
  }
}
} // namespace anari_cat
