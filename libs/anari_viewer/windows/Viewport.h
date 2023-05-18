// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Orbit.h"
#include "../ui_anari.h"
// anari
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// match3D
#include <match3D/match3D.h>
// std
#include <array>
#include <limits>

namespace windows {

struct Viewport : public match3D::Window
{
  Viewport(anari::Library library,
      anari::Device device,
      const char *name = "Viewport");
  ~Viewport();

  void buildUI() override;

  void setWorld(anari::World world = nullptr, bool resetCameraView = true);

  void setManipulator(manipulators::Orbit *m);

  void resetView(bool resetAzEl = true);

  anari::Device device() const;

 private:
  void reshape(anari::int2 newWindowSize);

  void startNewFrame();
  void updateFrame();
  void updateCamera(bool force = false);
  void updateImage();
  void cancelFrame();

  void ui_handleInput();
  void ui_contextMenu();
  void ui_overlay();

  // Data /////////////////////////////////////////////////////////////////////

  anari::float2 m_previousMouse{-1.f, -1.f};
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

  anari::Library m_library{nullptr};
  anari::Device m_device{nullptr};
  anari::Frame m_frame{nullptr};
  anari::World m_world{nullptr};

  anari::Camera m_perspCamera{nullptr};
  anari::Camera m_orthoCamera{nullptr};

  std::vector<std::string> m_rendererNames;
  std::vector<ui::ParameterList> m_rendererParameters;
  std::vector<anari::Renderer> m_renderers;
  int m_currentRenderer{0};

  // camera manipulator

  manipulators::Orbit m_localArcball;
  manipulators::Orbit *m_arcball{nullptr};
  manipulators::UpdateToken m_cameraToken{0};

  // OpenGL + display

  GLuint m_framebufferTexture{0};
  anari::int2 m_viewportSize{1920, 1080};
  anari::int2 m_renderSize{1920, 1080};

  float m_latestFL{1.f};
  float m_minFL{std::numeric_limits<float>::max()};
  float m_maxFL{-std::numeric_limits<float>::max()};

  std::string m_overlayWindowName;
  std::string m_contextMenuName;
};

} // namespace windows
