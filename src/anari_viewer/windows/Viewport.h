// Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Orbit.h"
#include "../ui_anari.h"
// glad
#include "glad/glad.h"
// glfw
#include <GLFW/glfw3.h>
// anari
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <limits>

#include "Window.h"

namespace anari_viewer::windows {

struct Viewport;
struct Overlay
{
  virtual void buildUI(Viewport *viewport) = 0;
};

using ViewportFrameReadyCallback = std::function<void(const void *,
    const anari_viewer::windows::Viewport *,
    const float /*duration*/)>;
struct Viewport : public Window
{
  Viewport(anari::Device device,
      const char *name = "Viewport",
      bool useOrthoCamera = false,
      int initRendererId = 0);
  ~Viewport();

  void buildUI() override;

  void setWorld(anari::World world = nullptr, bool resetCameraView = true);

  void setManipulator(manipulators::Orbit *m);

  void resetView(bool resetAzEl = true);

  anari::Device device() const;

  void setViewportFrameReadyCallback(
      ViewportFrameReadyCallback cb, void *userData);

  void addOverlay(Overlay *overlay);

 private:
  void reshape(anari::math::int2 newWindowSize);

  void startNewFrame();
  void updateFrame();
  void updateCamera(bool force = false);
  void updateImage();
  void cancelFrame();

  void ui_handleInput();
  void ui_contextMenu();
  void ui_overlay();

  // Data /////////////////////////////////////////////////////////////////////

  anari::math::float2 m_previousMouse{-1.f, -1.f};
  bool m_mouseRotating{false};
  bool m_manipulating{false};
  bool m_currentlyRendering{true};
  bool m_contextMenuVisible{false};
  bool m_frameCancelled{false};
  bool m_saveNextFrame{false};
  int m_screenshotIndex{0};

  bool m_showOverlay{true};
  int m_frameSamples{0};
  bool m_useOrthoCamera{false};

  float m_fov{40.f};

  // ANARI objects //

  anari::DataType m_format{ANARI_UFIXED8_RGBA_SRGB};

  anari::Device m_device{nullptr};
  anari::Frame m_frame{nullptr};
  anari::World m_world{nullptr};

  anari::Camera m_perspCamera{nullptr};
  anari::Camera m_orthoCamera{nullptr};

  std::vector<std::string> m_rendererNames;
  std::vector<ui::ParameterInfoList> m_rendererParameters;
  std::vector<anari::Renderer> m_renderers;
  int m_currentRenderer{0};

  // camera manipulator

  int m_arcballUp{1};
  manipulators::Orbit m_localArcball;
  manipulators::Orbit *m_arcball{nullptr};
  manipulators::UpdateToken m_cameraToken{0};
  float m_apertureRadius{0.f};
  float m_focusDistance{1.f};

  // OpenGL + display

  GLuint m_framebufferTexture{0};
  anari::math::int2 m_viewportSize{1920, 1080};
  anari::math::int2 m_renderSize{1920, 1080};

  float m_latestFL{1.f};
  float m_minFL{std::numeric_limits<float>::max()};
  float m_maxFL{-std::numeric_limits<float>::max()};

  std::string m_overlayWindowName;
  std::string m_contextMenuName;

  ViewportFrameReadyCallback m_onViewportFrameReady = nullptr;
  void *m_onViewportFrameReadyUserData = nullptr;

  std::vector<std::unique_ptr<Overlay>> m_overlays;
};

} // namespace anari_viewer::windows
