// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <SDL3/SDL.h>
// std
#include <memory>
#include <string_view>
#include <vector>

namespace anari_viewer {

struct AppImpl;

namespace windows {
struct Window;
}
using WindowArray = std::vector<std::unique_ptr<windows::Window>>;

class Application
{
 public:
  Application();
  virtual ~Application() = default;

  // Construct windows used by the application
  virtual WindowArray setupWindows() = 0;
  // This is called at the beginning of every iteration in the main event loop.
  virtual void mainLoopStart();
  // This is called before ImGui on every frame (ex: ImGui main menu bar)
  virtual void uiFrameStart();
  // This is called before the frame is rendered by graphics API (OpenGL)
  virtual void uiRenderStart();
  // This is called after the graphics API renders the frame.
  virtual void uiRenderEnd();
  // This is called after all ImGui calls are done on every frame
  virtual void uiFrameEnd();
  // This is called at the end of every iteration in the main event loop.
  virtual void mainLoopEnd();
  // Allow teardown of objects before application destruction
  virtual void teardown() = 0;

  SDL_Renderer* sdlRenderer();

  // Start the application run loop
  void run(int width, int height, const char *name);

 protected:
  bool getWindowSize(int &width, int &height) const;
  float getLastFrameLatency() const;

 private:
  void mainLoop();

  std::shared_ptr<AppImpl> m_impl;
};

} // namespace anari_viewer
