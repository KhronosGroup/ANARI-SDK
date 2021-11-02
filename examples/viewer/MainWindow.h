// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "OrbitManipulator.h"
#include "glm_box3.h"
// glad header
#include "glad/glad.h"
// glfw header
#include "GLFW/glfw3.h"
// ANARI headers
#include "anari/anari_cpp.hpp"
#include "anari/anari_cpp/ext/glm.h"
// ANARI sample scenes
#include "anari_test_scenes.h"
// C++ std
#include <memory>
#include <string>
#include <vector>

class MainWindow
{
 public:
  MainWindow(const glm::uvec2 &windowSize);
  ~MainWindow();

  void setDevice(ANARIDevice device);
  void setScene(
      std::string sceneName, std::string paramName = "", anari::Any param = {});

  void mainLoop();

 private:
  void addObjectToCommit(ANARIObject obj);

  void updateCamera();
  void resetCamera();

  void reshape(const glm::uvec2 &newWindowSize);
  void motion(const glm::vec2 &position);
  void display();

  void buildUI();

  void commitOutstandingHandles();

  void cleanup();

  friend void frame_continuation_callback(void *, ANARIDevice, ANARIFrame);
  friend void frame_show(ANARIDevice, ANARIFrame, MainWindow*);

  static MainWindow *activeWindow;

  glm::vec2 previousMouse{-1.f, -1.f};
  bool mouseRotating{false};

  glm_box3 bounds;
  float zoomSpeed{1.f};

  glm::vec4 bgColor{0.2f, 0.2f, 0.2f, 1.f};

  bool showUI{true};

  manipulators::Orbit arcball;

  // ANARI objects not managed by this class
  ANARIDevice device{nullptr};
  bool useContinuation = false;

  // ANARI objects managed by this class
  ANARIRenderer renderer{nullptr};
  ANARICamera camera{nullptr};
  ANARIFrame frame{nullptr};

  anari::scenes::SceneHandle scene;
  std::vector<anari::scenes::ParameterInfo> sceneParams;

  // Collection of ANARI handles to commit before the next frame
  std::vector<ANARIObject> objectsToCommit;
  bool commitScene{true};
  bool resetCameraPosition{true};

  std::vector<unsigned char> pixelBuffer;
  glm::ivec2 prevFrameSize{0};
  glm::ivec2 currentFrameSize{0};
  glm::ivec2 nextFrameSize{0};

  // OpenGL framebuffer texture
  GLuint framebufferTexture = 0;
  GLuint framebufferObject = 0;

  // FPS measurement of last frame
  float latestFPS{0.f};
};
