// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "MainWindow.h"
// std headers
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
// imgui headers
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_stdlib.h"
// stb_image
#include "stb_image_write.h"

static GLFWwindow *g_window = nullptr;
static bool g_quitNextFrame = false;
static bool g_saveNextFrame = false;
MainWindow *MainWindow::activeWindow = nullptr;

/* list of scene options for menu */
static const std::vector<std::string> g_scenes = {
    //
    "random_spheres",
    "cornell_box",
    "instanced_cubes",
    "textured_cube",
    "gravity_spheres_volume",
    "cornell_box_directional",
    "cornell_box_point",
    "cornell_box_spot",
    "cornell_box_ring",
    "cornell_box_quad",
    //"cornell_box_hdri",
    "cornell_box_multilight",
    "cornell_box_quad_geom",
    "cornell_box_cone_geom",
    "cornell_box_cylinder_geom",
    "textured_cube_samplers",
    "volume_test",
    "file_obj",
    //
};

static bool sceneUI_callback(void *, int index, const char **out_text)
{
  *out_text = g_scenes[index].c_str();
  return true;
}

using UIParameter = anari::scenes::ParameterInfo;
using UiFcn = void (*)(UIParameter &, anari::scenes::SceneHandle);

static void ui_bool(UIParameter &p, anari::scenes::SceneHandle s)
{
  bool &value = p.value.get<bool>();
  if (ImGui::Checkbox(p.name.c_str(), &value))
    anari::scenes::setParameter(s, p.name, value);
}

static void ui_int(UIParameter &p, anari::scenes::SceneHandle s)
{
  int &value = p.value.get<int>();
  if (ImGui::InputInt(p.name.c_str(), &value))
    anari::scenes::setParameter(s, p.name, value);
}

static void ui_float(UIParameter &p, anari::scenes::SceneHandle s)
{
  float &value = p.value.get<float>();
  if (ImGui::InputFloat(p.name.c_str(), &value))
    anari::scenes::setParameter(s, p.name, value);
}

static void ui_str(UIParameter &p, anari::scenes::SceneHandle s)
{
  std::string &value = p.value.get<std::string>();
  if (ImGui::InputText(p.name.c_str(), &value))
    anari::scenes::setParameter(s, p.name, value);
}

static std::unordered_map<int, UiFcn> g_uiFcns = {
    //
    {ANARI_BOOL, ui_bool},
    {ANARI_INT32, ui_int},
    {ANARI_FLOAT32, ui_float},
    {ANARI_STRING, ui_str}
    //
};

/* fbBytes(): return the memory needed for a FrameBuffer of widthxheight */
static size_t fbBytes(int width, int height)
{
  return size_t(4) * width * height;
}

/* frame_show(): */
void frame_show(ANARIDevice dev, ANARIFrame frame, MainWindow *window)
{
  if (frame != nullptr) {
    float duration = 0.f;
    anariGetProperty(dev,
        frame,
        "duration",
        ANARI_FLOAT32,
        &duration,
        sizeof(duration),
        ANARI_NO_WAIT);

    // Assume frames-per-second is reciprical of frame render duration
    window->latestFPS = 1.f / duration;

    auto *fb = anariMapFrame(dev, frame, "color");

    size_t numBytes =
        fbBytes(window->currentFrameSize.x, window->currentFrameSize.y);
    window->pixelBuffer.resize(numBytes);
    std::memcpy(window->pixelBuffer.data(), fb, numBytes);

    window->prevFrameSize = window->currentFrameSize;

    // When flagged: save the current frame to a file
    if (g_saveNextFrame) {
      stbi_write_png("viewer_screenshot.png",
          window->currentFrameSize.x,
          window->currentFrameSize.y,
          4,
          fb,
          4 * window->currentFrameSize.x);
      std::cout << "finished writing 'viewer_screenshot.png'" << std::endl;
      g_saveNextFrame = false;
    }

    anariUnmapFrame(dev, frame, "color");

    window->updateScene();
  }
}

void frame_continuation_callback(void *w, ANARIDevice dev, ANARIFrame frame)
{
  if (g_quitNextFrame)
    return;

  auto *window = (MainWindow *)w;

  frame_show(dev, frame, window);

  window->currentFrameSize = window->nextFrameSize;
  anariRenderFrame(dev, window->frame);
}

// MainWindow definitions /////////////////////////////////////////////////////

/* MainWindow constructor(): takes a 2D vector for the window size, and */
/*  initializes the window setting the "activeWindow" field to          */
/*  self-referentially point to this window.                            */
MainWindow::MainWindow(const glm::uvec2 &windowSize)
{
  if (activeWindow != nullptr) {
    throw std::runtime_error("Cannot create more than one MainWindow!");
  }

  activeWindow = this;

  // STB image library is Y-down, OpenGL is Y-up
  stbi_flip_vertically_on_write(1);

  // Set the GLFW Error callback to a lambda expression that outputs to stderr
  glfwSetErrorCallback([](int error, const char *desc) {
    std::cerr << "GLFW error " << error << ": " << desc << std::endl;
  });

  // initialize GLFW Cross-platform windowing library
  if (!glfwInit()) {
    throw std::runtime_error("Failed to initialize GLFW!");
  }

#ifdef __APPLE__
  // On macOS, make sure we get a 3.2 core context
  glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint (GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint (GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  // On retina display, render with low resolution and scale up
  glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
#endif

  // Request a window that is IEC standard RGB color space capable
  glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);

  // create GLFW window
  g_window = glfwCreateWindow(windowSize.x,
      windowSize.y,
      "ANARI",
      /*fullscreen=*/nullptr,
      /*share=*/nullptr);

  if (!g_window) {
    glfwTerminate();
    throw std::runtime_error("Failed to create GLFW window!");
  }

  // make the window's context current
  glfwMakeContextCurrent(g_window);

  // Setup for OpenGL graphics library rendering
  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    glfwTerminate();
    throw std::runtime_error("Failed to load GL!");
  }

  // Create a texture
  glGenTextures(1, &framebufferTexture);
  glBindTexture(GL_TEXTURE_2D, framebufferTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D,
      0,
      GL_RGBA8,
      windowSize.x,
      windowSize.y,
      0,
      GL_RGBA,
      GL_UNSIGNED_BYTE,
      0);

  // Map framebufferTexture (above) to a new OpenGL read/write framebuffer
  glGenFramebuffers(1, &framebufferObject);
  glBindFramebuffer(GL_FRAMEBUFFER, framebufferObject);
  glFramebufferTexture2D(GL_FRAMEBUFFER,
      GL_COLOR_ATTACHMENT0,
      GL_TEXTURE_2D,
      framebufferTexture,
      0);
  glReadBuffer(GL_COLOR_ATTACHMENT0);

  // Setup the ImGui interface panel
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(g_window, true);
  ImGui_ImplOpenGL3_Init();

  ImGuiIO &io = ImGui::GetIO();
  io.FontGlobalScale = 1.25f;

  // set GLFW callbacks
  glfwSetFramebufferSizeCallback(g_window,
      // Lambda function that take GLFW window size and applies it to the
      // MainWindow
      [](GLFWwindow *, int newWidth, int newHeight) {
        activeWindow->reshape(glm::uvec2{newWidth, newHeight});
      });

  glfwSetCursorPosCallback(g_window,
      // Lambda function that calls ImGui library to get the mouse location
      [](GLFWwindow *, double x, double y) {
        ImGuiIO &io = ImGui::GetIO();
        if (!io.WantCaptureMouse)
          activeWindow->motion(glm::vec2{float(x), float(y)});
      });

  // Do not wait for screen updates before swapping buffers
  glfwSwapInterval(0);
}

void MainWindow::setDevice(ANARIDevice dev)
{
  device = dev;

  // Enable the "continuation rendering" feature profile
  useContinuation =
      anariDeviceImplements(device, ANARI_KHR_FRAME_COMPLETION_CALLBACK);

  camera = anariNewCamera(dev, "perspective");
  //anari::setParameter(dev, camera, "position", glm::vec3(3.5f, 3.5f, 3.5f));
  //anari::setParameter(dev, camera, "direction", glm::vec3(1.5f, 1.5f, 1.5f));
  //anari::setParameter(dev, camera, "up", glm::vec3(1.5f, 1.5f, 1.5f));
  anariCommit(dev, camera);

  renderer = anariNewRenderer(dev, "default");
  anari::setParameter(dev, renderer, "backgroundColor", bgColor);
  anariCommit(dev, renderer);

  frame = anariNewFrame(device);

  if (useContinuation) {
    anari::setParameter<ANARIFrameCompletionCallback>(
        dev, frame, "frameCompletionCallback", &frame_continuation_callback);
    anari::setParameter<void *>(
        dev, frame, "frameCompletionCallbackUserData", this);
  }

  glm::uvec2 windowSize;
  glfwGetWindowSize(g_window, (int *)&windowSize.x, (int *)&windowSize.y);
  reshape(windowSize);
}

/* MainWindow() Destructor */
MainWindow::~MainWindow()
{
  activeWindow = nullptr;
}

/* resetCamera(): */
void MainWindow::resetCamera()
{
  auto _bounds = anari::scenes::getBounds(scene);
  glm_box3 bounds;

  std::memcpy(&bounds, &_bounds, sizeof(bounds));
  arcball = manipulators::Orbit(bounds);
  zoomSpeed = 0.01f * glm::length(bounds.size());
  updateCamera();
}

void MainWindow::setScene(
    std::string sceneName, std::string paramName, anari::Any param)
{
  auto s = anari::scenes::createScene(device, sceneName.c_str());
  try {
    auto params = anari::scenes::getParameters(s);

    if (!paramName.empty()) {
      anari::scenes::setParameter(s, paramName, param);
      for (auto &p : params)
        if (paramName == p.name)
          p.value = param;
    }

    printf("Setting the scene to %s\n", sceneName.c_str());

    commitScene = true;

    sceneParams = params;
    scene = s;

    anari::setParameter(device, frame, "world", anari::scenes::getWorld(s));
    addObjectToCommit(frame);

    resetCameraPosition = true;
  } catch (const std::runtime_error &e) {
    printf("failed to create '%s' scene: %s\n", sceneName.c_str(), e.what());
  }
}

void MainWindow::updateCamera()
{
  //anari::setParameter(device, camera, "position", glm::vec3(0.f, 0.f, 0.f));
  //anari::setParameter(device, camera, "direction", glm::vec3(0.f, 0.f, 0.f));
  //anari::setParameter(device, camera, "up", glm::vec3(0.f, 0.f, 0.f));
  anari::setParameter(device, camera, "position", arcball.eye());
  anari::setParameter(device, camera, "direction", arcball.dir());
  anari::setParameter(device, camera, "up", arcball.up());
  addObjectToCommit(camera);
}

/* mainLoop(): continually loop until a window-close event is recieved */
/*   or the user-interface sets the "g_quitNextFrame" flag.  Choose    */
/*   between the continually-improving rendering profile vs. the       */
/*   render-once profile.                                              */
void MainWindow::mainLoop()
{
  updateScene();

  if (useContinuation) {
    // Continuation rendering profile
    frame_continuation_callback(this, device, nullptr);

    while (!glfwWindowShouldClose(g_window) && !g_quitNextFrame) {
      // Update the User-Interface
      glfwPollEvents();
      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();
      buildUI();

      // output the rendering
      display();
    }

  } else {
    // Non-continuation rendering profile
    anariRenderFrame(device, frame);

    while (!glfwWindowShouldClose(g_window) && !g_quitNextFrame) {
      // Update the User-Interface
      glfwPollEvents();
      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();
      buildUI();

      // output the rendering
      if (anariFrameReady(device, frame, ANARI_NO_WAIT)) {
        // update frame if a new one is available
        frame_show(device, frame, this);
        currentFrameSize = nextFrameSize;
        display();
        anariRenderFrame(device, frame);
      } else {
        // otherwise just present with updated UI
        display();
      }
    }
  }

  g_quitNextFrame = true;
  anariFrameReady(device, frame, ANARI_WAIT);
  cleanup();
}

void MainWindow::reshape(const glm::uvec2 &windowSize)
{
  anari::setParameter(device, frame, "size", windowSize);
  anari::setParameter(device, frame, "color", ANARI_UFIXED8_RGBA_SRGB);
  anari::setParameter(device, frame, "renderer", renderer);
  anari::setParameter(device, frame, "camera", camera);

  addObjectToCommit(frame);

  nextFrameSize = windowSize;

  // reset OpenGL viewport and orthographic projection
  glViewport(0, 0, windowSize.x, windowSize.y);

  glBindTexture(GL_TEXTURE_2D, framebufferTexture);
  glTexImage2D(GL_TEXTURE_2D,
      0,
      GL_RGBA8,
      windowSize.x,
      windowSize.y,
      0,
      GL_RGBA,
      GL_UNSIGNED_BYTE,
      0);

  anari::setParameter(device, camera, "aspect", windowSize.x / float(windowSize.y));
  addObjectToCommit(camera);
}

void MainWindow::motion(const glm::vec2 &position)
{
  const glm::vec2 mouse(position.x, position.y);

  const bool leftDown =
      glfwGetMouseButton(g_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  const bool rightDown =
      glfwGetMouseButton(g_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
  const bool middleDown =
      glfwGetMouseButton(g_window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;

  const bool anyDown = leftDown || rightDown || middleDown;

  if (mouseRotating && !leftDown)
    mouseRotating = false;

  if (anyDown && previousMouse != glm::vec2(-1)) {
    const glm::vec2 prev = previousMouse;

    glm::ivec2 windowSize;
    glfwGetFramebufferSize(g_window, &windowSize.x, &windowSize.y);

    const glm::vec2 mouseFrom = prev * 2.f / glm::vec2(windowSize);
    const glm::vec2 mouseTo = mouse * 2.f / glm::vec2(windowSize);

    const glm::vec2 mouseDelta = mouseFrom - mouseTo;

    if (leftDown) {
      if (!mouseRotating) {
        arcball.startNewRotation();
        mouseRotating = true;
      }

      arcball.rotate(mouseDelta);
    } else if (rightDown)
      arcball.zoom(mouseDelta.y);
    else if (middleDown)
      arcball.pan(mouseDelta);

    updateCamera();
  }

  previousMouse = mouse;
}

void MainWindow::display()
{
  glm::ivec2 windowSize;
  glfwGetFramebufferSize(g_window, &windowSize.x, &windowSize.y);

  std::stringstream windowTitle;
  windowTitle << "ANARI: " << std::setprecision(3) << latestFPS << " fps";
  glfwSetWindowTitle(g_window, windowTitle.str().c_str());

  if (!pixelBuffer.empty()) {
    glBindTexture(GL_TEXTURE_2D, framebufferTexture);
    glTexSubImage2D(GL_TEXTURE_2D,
        0,
        0,
        0,
        prevFrameSize.x,
        prevFrameSize.y,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixelBuffer.data());
  }

  glBindFramebuffer(GL_READ_FRAMEBUFFER, framebufferObject);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

  glClear(GL_COLOR_BUFFER_BIT);
  glBlitFramebuffer(0,
      0,
      windowSize.x,
      windowSize.y,
      0,
      0,
      windowSize.x,
      windowSize.y,
      GL_COLOR_BUFFER_BIT,
      GL_NEAREST);

  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  glfwSwapBuffers(g_window);
}

void MainWindow::buildUI()
{
  if (!showUI)
    return;

  ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration
      | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
      | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
      | ImGuiWindowFlags_NoMove;

  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);

  ImGui::Begin("ANARI Viewer", &showUI, windowFlags);

  ImGui::Text("App:");

  if (ImGui::Button("Take Screenshot"))
    g_saveNextFrame = true;

  ImGui::SameLine();

  if (ImGui::Button("Reset View"))
    resetCamera();

  ImGui::SameLine();

  if (ImGui::Button("Quit"))
    g_quitNextFrame = true;

  ImGui::Separator();

  ImGui::Text("Renderer:");

  if (ImGui::ColorEdit4("background", &bgColor.x)) {
    anari::setParameter(device, renderer, "backgroundColor", bgColor);
    addObjectToCommit(renderer);
  }

  ImGui::Separator();

  ImGui::Text("Scene:");

  static int whichScene = 0;
  if (ImGui::Combo("scene##whichScene",
          &whichScene,
          sceneUI_callback,
          nullptr,
          g_scenes.size())) {
    setScene(g_scenes[whichScene]);
  }

  for (auto &p : sceneParams) {
    auto fcn = g_uiFcns[p.type];
    if (fcn)
      fcn(p, scene);
    else
      ImGui::Text("UI FOR TYPE NOT IMPLEMENTED -> %s", p.name.c_str());
  }

  if (!sceneParams.empty() && ImGui::Button("Apply")) {
    commitScene = true;
    resetCameraPosition = true;
  }

  ImGui::End();
}

void MainWindow::addObjectToCommit(ANARIObject obj)
{
  anariCommit(device, obj);
}

void MainWindow::updateScene()
{
  if (commitScene) {
    try {
      anari::scenes::commit(scene);
    } catch (const std::runtime_error &e) {
      printf("failed to update scene: %s\n", e.what());
    }
    commitScene = false;
  }

  if (resetCameraPosition) {
    resetCamera();
    resetCameraPosition = false;
  }
}

void MainWindow::cleanup()
{
  anariRelease(device, renderer);
  anariRelease(device, camera);
  anariRelease(device, frame);

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(g_window);
  glfwTerminate();
}
