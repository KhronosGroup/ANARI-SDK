// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Application.h"
// glad
#include "glad/glad.h"
// glfw
#include <GLFW/glfw3.h>
// imgui
#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
// std
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace anari_viewer {

static void glfw_error_callback(int error, const char *description)
{
  fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

struct AppImpl
{
  GLFWwindow *window{nullptr};
  int width{0};
  int height{0};
  bool windowResized{true};
  std::string name;

  std::chrono::time_point<std::chrono::steady_clock> frameEndTime;
  std::chrono::time_point<std::chrono::steady_clock> frameStartTime;

  WindowArray windows;

  void init();
  void renderWindows();
  void cleanup();
};

Application::Application()
{
  m_impl = std::make_shared<AppImpl>();
  glfwSetErrorCallback(glfw_error_callback);
}

void Application::uiFrameStart()
{
  // no-op
}

void Application::uiFrameEnd()
{
  // no-op
}

void Application::run(int width, int height, const char *name)
{
  m_impl->width = width;
  m_impl->height = height;
  m_impl->name = name;

  m_impl->init();
  m_impl->windows = setupWindows();
  mainLoop();
  teardown();
  m_impl->cleanup();
}

bool Application::getWindowSize(int &width, int &height) const
{
  width = m_impl->width;
  height = m_impl->height;
  return m_impl->windowResized;
}

float Application::getLastFrameLatency() const
{
  auto diff = m_impl->frameEndTime - m_impl->frameStartTime;
  return std::chrono::duration<float>(diff).count();
}

void Application::mainLoop()
{
  auto window = m_impl->window;

  while (!glfwWindowShouldClose(window)) {
    m_impl->frameStartTime = m_impl->frameEndTime;
    m_impl->frameEndTime = std::chrono::steady_clock::now();
    glfwPollEvents();

    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplGlfw_NewFrame();

    ImGui::NewFrame();

    ImGuiIO &io = ImGui::GetIO();
    if (io.KeysDown[GLFW_KEY_Q] && io.KeysDown[GLFW_KEY_LEFT_CONTROL])
      glfwSetWindowShouldClose(window, 1);

    uiFrameStart();

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("MainDockSpace", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("MainDockSpaceID");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    m_impl->renderWindows();

    ImGui::End();

    ImGui::Render();

    glClearColor(0.1f, 0.1f, 0.1f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
    m_impl->windowResized = false;

    uiFrameEnd();
  }
}

void AppImpl::init()
{
  if (!glfwInit())
    throw std::runtime_error("failed to initialize GLFW");

  glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);

  window = glfwCreateWindow(width, height, name.c_str(), nullptr, nullptr);
  if (window == nullptr)
    throw std::runtime_error("failed to create GLFW window");

  glfwSetWindowUserPointer(window, this);

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    glfwTerminate();
    throw std::runtime_error("Failed to load GL");
  }

  glfwSetFramebufferSizeCallback(
      window, [](GLFWwindow *w, int newWidth, int newHeight) {
        auto *app = (AppImpl *)glfwGetWindowUserPointer(w);
        app->width = newWidth;
        app->height = newHeight;
        app->windowResized = true;
      });

  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL2_Init();

  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  ImGui::StyleColorsDark();

  ImGuiStyle &style = ImGui::GetStyle();

  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;

  style.WindowRounding = 0.0f;
  style.ChildRounding = 0.f;
  style.FrameRounding = 0.f;
  style.PopupRounding = 0.f;
  style.ScrollbarRounding = 0.f;
  style.GrabRounding = 0.f;
  style.TabRounding = 0.f;
}

void AppImpl::renderWindows()
{
  for (auto &w : windows)
    w->renderUI();
}

void AppImpl::cleanup()
{
  windows.clear();

  ImGui_ImplOpenGL2_Shutdown();
  ImGui_ImplGlfw_Shutdown();

  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  window = nullptr;
}

} // namespace anari_viewer