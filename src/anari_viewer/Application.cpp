// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Application.h"
#include "windows/Window.h"
// sdl
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
// imgui
#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
// std
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace anari_viewer {

struct AppImpl
{
  SDL_Window *sdl_window{nullptr};
  SDL_Renderer *sdl_renderer{nullptr};
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
}

void Application::mainLoopStart()
{
  // no-op
}

void Application::uiFrameStart()
{
  // no-op
}

void Application::uiRenderStart()
{
  // no-op
}

void Application::uiRenderEnd()
{
  // no-op
}

void Application::uiFrameEnd()
{
  // no-op
}

void Application::mainLoopEnd()
{
  // no-op
}

SDL_Renderer *Application::sdlRenderer()
{
  return m_impl->sdl_renderer;
}

SDL_Window *Application::sdlWindow()
{
  return m_impl->sdl_window;
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
  auto window = sdlWindow();

  bool open = true;
  while (open) {
    m_impl->frameStartTime = m_impl->frameEndTime;
    m_impl->frameEndTime = std::chrono::steady_clock::now();
    mainLoopStart();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT)
        open = false;
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
          && event.window.windowID == SDL_GetWindowID(window))
        open = false;
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();

    ImGui::NewFrame();

    if (ImGui::IsKeyChordPressed(ImGuiKey_Q | ImGuiMod_Ctrl))
      open = false;

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

    // Enclose opengl calls inside uiRenderStart/uiRenderEnd
    uiRenderStart();
    ImGuiIO &io = ImGui::GetIO();
    m_impl->width = io.DisplaySize.x;
    m_impl->height = io.DisplaySize.y;
    auto sdl_renderer = m_impl->sdl_renderer;
    SDL_SetRenderDrawColorFloat(sdl_renderer, 0.1f, 0.1f, 0.1f, 1.f);
    SDL_RenderClear(sdl_renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), sdl_renderer);
    SDL_RenderPresent(sdl_renderer);
    uiRenderEnd();

    m_impl->windowResized = false;

    uiFrameEnd();
    mainLoopEnd();
  }
}

void AppImpl::init()
{
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    throw std::runtime_error("failed to initialize SDL");

  Uint32 window_flags =
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
  sdl_window = SDL_CreateWindow(name.c_str(), width, height, window_flags);

  if (sdl_window == nullptr)
    throw std::runtime_error("failed to create SDL window");

  sdl_renderer = SDL_CreateRenderer(sdl_window, nullptr);

  SDL_SetWindowPosition(
      sdl_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  if (sdl_renderer == nullptr) {
    SDL_DestroyWindow(sdl_window);
    SDL_Quit();
    throw std::runtime_error("Failed to create SDL renderer");
  }

  SDL_ShowWindow(sdl_window);

  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  ImGui_ImplSDL3_InitForSDLRenderer(sdl_window, sdl_renderer);
  ImGui_ImplSDLRenderer3_Init(sdl_renderer);

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

  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();

  SDL_DestroyRenderer(sdl_renderer);
  SDL_DestroyWindow(sdl_window);
  SDL_Quit();

  sdl_window = nullptr;
}

} // namespace anari_viewer
