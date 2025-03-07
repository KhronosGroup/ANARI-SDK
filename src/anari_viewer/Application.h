// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "windows/Window.h"
// std
#include <memory>
#include <string_view>
#include <vector>

namespace anari_viewer {

struct AppImpl;
using WindowArray = std::vector<std::unique_ptr<windows::Window>>;

class Application
{
 public:
  Application();
  virtual ~Application() = default;

  // Construct windows used by the application
  virtual WindowArray setupWindows() = 0;
  // This is called before ImGui on every frame (ex: ImGui main menu bar)
  virtual void uiFrameStart();
  // This is called after all ImGui calls are done on every frame
  virtual void uiFrameEnd();
  // Allow teardown of objects before application destruction
  virtual void teardown() = 0;

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
