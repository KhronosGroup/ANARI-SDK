// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Viewport.h"
// std
#include <cstring>
// stb_image
#include "stb_image_write.h"

namespace windows {

///////////////////////////////////////////////////////////////////////////////
// Viewport definitions ///////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

Viewport::Viewport(
    anari::Library library, anari::Device device, const char *name)
    : Window(name, true), m_library(library), m_device(device)
{
  setManipulator(nullptr);

  m_overlayWindowName = "overlay_";
  m_overlayWindowName += name;

  m_contextMenuName = "vpContextMenu_";
  m_contextMenuName += name;

  // GL //

  glGenTextures(1, &m_framebufferTexture);
  glBindTexture(GL_TEXTURE_2D, m_framebufferTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D,
      0,
      GL_RGBA8,
      m_viewportSize.x,
      m_viewportSize.y,
      0,
      GL_RGBA,
      GL_UNSIGNED_BYTE,
      0);

  // ANARI //

  anari::retain(m_device, m_device);

  const char **r_subtypes =
      anariGetObjectSubtypes(m_library, "default", ANARI_RENDERER);

  if (r_subtypes != nullptr) {
    for (int i = 0; r_subtypes[i] != nullptr; i++)
      m_rendererNames.push_back(r_subtypes[i]);
  } else
    m_rendererNames.emplace_back("default");

  anari::commitParameters(m_device, m_device);

  m_frame = anari::newObject<anari::Frame>(m_device);
  m_perspCamera = anari::newObject<anari::Camera>(m_device, "perspective");
  m_orthoCamera = anari::newObject<anari::Camera>(m_device, "orthographic");

  for (auto &name : m_rendererNames) {
    m_renderers.push_back(
        anari::newObject<anari::Renderer>(m_device, name.c_str()));
  }

  m_currentRenderer = m_renderers[0];

  reshape(m_viewportSize);
  setWorld();
  updateFrame();
  updateCamera(true);
  startNewFrame();
}

Viewport::~Viewport()
{
  cancelFrame();
  anari::wait(m_device, m_frame);

  anari::release(m_device, m_perspCamera);
  anari::release(m_device, m_orthoCamera);
  anari::release(m_device, m_world);
  for (auto &r : m_renderers)
    anari::release(m_device, r);
  anari::release(m_device, m_frame);
  anari::release(m_device, m_device);
}

void Viewport::buildUI()
{
  ImVec2 _viewportSize = ImGui::GetContentRegionAvail();
  anari::int2 viewportSize(_viewportSize.x, _viewportSize.y);

  if (m_viewportSize != viewportSize)
    reshape(viewportSize);

  updateImage();
  updateCamera();

  ImGui::Image(
      (void *)(intptr_t)m_framebufferTexture, ImGui::GetContentRegionAvail());

  if (m_showOverlay)
    ui_overlay();

  ui_contextMenu();

  if (!m_contextMenuVisible)
    ui_handleInput();
}

void Viewport::setManipulator(manipulators::Orbit *m)
{
  m_arcball = m ? m : &m_localArcball;
}

void Viewport::setWorld(anari::World world, bool resetCameraView)
{
  if (m_world)
    anari::release(m_device, m_world);

  if (!world) {
    world = anari::newObject<anari::World>(m_device);
    resetCameraView = false;
  } else
    anari::retain(m_device, world);

  anari::commitParameters(m_device, world);
  m_world = world;

  if (resetCameraView)
    resetView();

  updateFrame();
}

void Viewport::resetView(bool resetAzEl)
{
  anari::float3 bounds[2];

  anariGetProperty(m_device,
      m_world,
      "bounds",
      ANARI_FLOAT32_BOX3,
      &bounds[0],
      sizeof(bounds),
      ANARI_WAIT);

#if 0
  printf("resetting view using bounds {%f, %f, %f} x {%f, %f, %f}\n",
      bounds[0].x,
      bounds[0].y,
      bounds[0].z,
      bounds[1].x,
      bounds[1].y,
      bounds[1].z);
#endif

  auto center = 0.5f * (bounds[0] + bounds[1]);
  auto diag = bounds[1] - bounds[0];

  auto azel = resetAzEl ? anari::float2(180.f, 200.f) : m_arcball->azel();
  m_arcball->setConfig(center, 1.25f * linalg::length(diag), azel);
  m_cameraToken = 0;
}

anari::Device Viewport::device() const
{
  return m_device;
}

void Viewport::reshape(anari::int2 newSize)
{
  if (newSize.x <= 0 || newSize.y <= 0)
    return;

  m_viewportSize = newSize;

  glViewport(0, 0, newSize.x, newSize.y);

  glBindTexture(GL_TEXTURE_2D, m_framebufferTexture);
  glTexImage2D(GL_TEXTURE_2D,
      0,
      GL_RGBA8,
      newSize.x,
      newSize.y,
      0,
      GL_RGBA,
      GL_UNSIGNED_BYTE,
      0);

  updateFrame();
  updateCamera(true);
}

void Viewport::startNewFrame()
{
  anari::getProperty(
      m_device, m_frame, "numSamples", m_frameSamples, ANARI_NO_WAIT);

  if (m_useFrameLimit && m_frameSamples >= m_frameLimit) {
    bool restartRendering = false;
    anari::getProperty(
        m_device, m_frame, "nextFrameReset", restartRendering, ANARI_NO_WAIT);
    if (!restartRendering)
      return;
  }

  anari::render(m_device, m_frame);
  m_renderSize = m_viewportSize;
  m_currentlyRendering = true;
  m_frameCancelled = false;
}

void Viewport::updateFrame()
{
  anari::setParameter(m_device, m_frame, "size", anari::uint2(m_viewportSize));
  anari::setParameter(
      m_device, m_frame, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  anari::setParameter(m_device, m_frame, "accumulation", true);

  anari::setParameter(m_device, m_frame, "world", m_world);
  if (m_useOrthoCamera)
    anari::setParameter(m_device, m_frame, "camera", m_orthoCamera);
  else
    anari::setParameter(m_device, m_frame, "camera", m_perspCamera);
  anari::setParameter(m_device, m_frame, "renderer", m_currentRenderer);

  anari::setParameter(m_device, m_frame, "checkerboard", m_checkerboard);

  anari::setParameter(m_device, m_currentRenderer, "background", m_background);
  anari::setParameter(
      m_device, m_currentRenderer, "ambientColor", m_ambientColor);
  anari::setParameter(
      m_device, m_currentRenderer, "ambientIntensity", m_ambientIntensity);
  anari::setParameter(m_device,
      m_currentRenderer,
      "ambientOcclusionDistance",
      m_ambientOcclusionDistance);
  anari::setParameter(
      m_device, m_currentRenderer, "pixelSamples", m_samplesPerFrame);

  anari::commitParameters(m_device, m_currentRenderer);
  anari::commitParameters(m_device, m_frame);
}

void Viewport::updateCamera(bool force)
{
  if (!force && !m_arcball->hasChanged(m_cameraToken))
    return;

  anari::setParameter(m_device, m_perspCamera, "position", m_arcball->eye());
  anari::setParameter(m_device, m_perspCamera, "direction", m_arcball->dir());
  anari::setParameter(m_device, m_perspCamera, "up", m_arcball->up());

  anari::setParameter(
      m_device, m_orthoCamera, "position", m_arcball->eye_FixedDistance());
  anari::setParameter(m_device, m_orthoCamera, "direction", m_arcball->dir());
  anari::setParameter(m_device, m_orthoCamera, "up", m_arcball->up());
  anari::setParameter(m_device, m_orthoCamera, "height", m_arcball->distance());

  anari::setParameter(m_device,
      m_perspCamera,
      "aspect",
      m_viewportSize.x / float(m_viewportSize.y));
  anari::setParameter(m_device,
      m_orthoCamera,
      "aspect",
      m_viewportSize.x / float(m_viewportSize.y));

  auto radians = [](float degrees) { return degrees * M_PI / 180.f; };
  anari::setParameter(m_device, m_perspCamera, "fovy", radians(m_fov));

  anari::commitParameters(m_device, m_perspCamera);
  anari::commitParameters(m_device, m_orthoCamera);
}

void Viewport::updateImage()
{
  if (m_frameCancelled)
    anari::wait(m_device, m_frame);
  else if (m_saveNextFrame
      || (m_currentlyRendering && anari::isReady(m_device, m_frame))) {
    m_currentlyRendering = false;

    float duration = 0.f;
    anari::getProperty(m_device, m_frame, "duration", duration);

    m_latestFL = duration * 1000;
    m_minFL = std::min(m_minFL, m_latestFL);
    m_maxFL = std::max(m_maxFL, m_latestFL);

    auto fb = anari::map<uint32_t>(m_device, m_frame, "channel.color");

    if (fb.data) {
      glBindTexture(GL_TEXTURE_2D, m_framebufferTexture);
      glTexSubImage2D(GL_TEXTURE_2D,
          0,
          0,
          0,
          fb.width,
          fb.height,
          GL_RGBA,
          GL_UNSIGNED_BYTE,
          fb.data);
    } else {
      printf("mapped bad frame: %p | %i x %i\n", fb.data, fb.width, fb.height);
    }

    if (m_saveNextFrame) {
      std::string filename =
          "screenshot" + std::to_string(m_screenshotIndex++) + ".png";
      stbi_write_png(
          filename.c_str(), fb.width, fb.height, 4, fb.data, 4 * fb.width);
      printf("frame saved to '%s'\n", filename.c_str());
      m_saveNextFrame = false;
    }

    anari::unmap(m_device, m_frame, "channel.color");
  }

  if (!m_currentlyRendering || m_frameCancelled)
    startNewFrame();
}

void Viewport::cancelFrame()
{
  m_frameCancelled = true;
  anari::discard(m_device, m_frame);
}

void Viewport::ui_handleInput()
{
  ImGuiIO &io = ImGui::GetIO();

  const bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  const bool rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
  const bool middleDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);

  const bool anyDown = leftDown || rightDown || middleDown;

  if (!anyDown) {
    m_manipulating = false;
    m_previousMouse = anari::float2(-1);
  } else if (ImGui::IsItemHovered() && !m_manipulating)
    m_manipulating = true;

  if (m_mouseRotating && !leftDown)
    m_mouseRotating = false;

  if (m_manipulating) {
    anari::float2 position;
    std::memcpy(&position, &io.MousePos, sizeof(position));

    const anari::float2 mouse(position.x, position.y);

    if (anyDown && m_previousMouse != anari::float2(-1)) {
      const anari::float2 prev = m_previousMouse;

      const anari::float2 mouseFrom =
          prev * 2.f / anari::float2(m_viewportSize);
      const anari::float2 mouseTo = mouse * 2.f / anari::float2(m_viewportSize);

      const anari::float2 mouseDelta = mouseFrom - mouseTo;

      if (mouseDelta != anari::float2(0.f)) {
        if (leftDown) {
          if (!m_mouseRotating) {
            m_arcball->startNewRotation();
            m_mouseRotating = true;
          }

          m_arcball->rotate(mouseDelta);
        } else if (rightDown)
          m_arcball->zoom(mouseDelta.y);
        else if (middleDown)
          m_arcball->pan(mouseDelta);
      }
    }

    m_previousMouse = mouse;
  }
}

void Viewport::ui_contextMenu()
{
  const bool rightClicked = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Right);

  if (rightClicked && ImGui::IsWindowHovered()) {
    m_contextMenuVisible = true;
    ImGui::OpenPopup(m_contextMenuName.c_str());
  }

  if (ImGui::BeginPopup(m_contextMenuName.c_str())) {
    if (ImGui::Checkbox("checkerboard", &m_checkerboard))
      updateFrame();

    if (ImGui::Checkbox("denoise", &m_denoise)) {
      anari::setParameter(m_device, m_frame, "denoise", m_denoise);
      anari::commitParameters(m_device, m_frame);
    }

    if (ImGui::BeginMenu("samplesPerFrame")) {
      if (ImGui::SliderInt("##samplesPerFrame", &m_samplesPerFrame, 1, 64))
        updateFrame();
      ImGui::EndMenu();
    }

    ImGui::Separator();

    if (ImGui::BeginMenu("renderer type")) {
      for (int i = 0; i < m_rendererNames.size(); i++) {
        if (ImGui::MenuItem(m_rendererNames[i].c_str())) {
          m_currentRenderer = m_renderers[i];
          updateFrame();
        }
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("frame limit")) {
      ImGui::Checkbox("enabled", &m_useFrameLimit);
      if (m_useFrameLimit) {
        ImGui::SetNextItemWidth(40);
        ImGui::DragInt("# frames", &m_frameLimit, 1.f, 1, 1024);
      }

      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("ambient light")) {
      ImGuiColorEditFlags misc_flags =
          ImGuiColorEditFlags_AlphaPreview | ImGuiColorEditFlags_NoOptions;
      bool update = ImGui::DragFloat(
          "intensity##ambient", &m_ambientIntensity, 0.001f, 0.f, 1000.f);

      update |= ImGui::ColorEdit3("color##ambient", &m_ambientColor.x);

      update |= ImGui::DragFloat("occlusion distance##ambient",
          &m_ambientOcclusionDistance,
          0.001f,
          0.f,
          100000.f);

      if (update)
        updateFrame();

      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("background")) {
      ImGuiColorEditFlags misc_flags =
          ImGuiColorEditFlags_AlphaPreview | ImGuiColorEditFlags_NoOptions;
      if (ImGui::ColorEdit4("##bgColor", &m_background.x, misc_flags))
        updateFrame();

      ImGui::EndMenu();
    }

    if (ImGui::MenuItem("restart rendering"))
      updateFrame();

    ImGui::Separator();

    if (ImGui::BeginMenu("camera")) {
      if (ImGui::Checkbox("orthographic", &m_useOrthoCamera))
        updateFrame();

      ImGui::BeginDisabled(m_useOrthoCamera);

      if (ImGui::SliderFloat("fov", &m_fov, 0.1f, 180.f))
        updateCamera(true);

      ImGui::EndDisabled();

      ImGui::Separator();

      if (ImGui::MenuItem("reset view"))
        resetView();

      ImGui::EndMenu();
    }

    ImGui::Separator();

    ImGui::Checkbox("show stats", &m_showOverlay);
    if (ImGui::MenuItem("reset stats")) {
      m_minFL = m_latestFL;
      m_maxFL = m_latestFL;
    }

    ImGui::Separator();

    if (ImGui::MenuItem("take screenshot"))
      m_saveNextFrame = true;

    if (!ImGui::IsPopupOpen(m_contextMenuName.c_str()))
      m_contextMenuVisible = false;

    ImGui::EndPopup();
  }
}

void Viewport::ui_overlay()
{
  ImGuiIO &io = ImGui::GetIO();
  ImVec2 windowPos = ImGui::GetWindowPos();
  windowPos.x += 10;
  windowPos.y += 25 * io.FontGlobalScale;

  ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration
      | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize
      | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
      | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

  ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);

  ImGui::Begin(m_overlayWindowName.c_str(), nullptr, window_flags);

  ImGui::Text("viewport: %i x %i", m_viewportSize.x, m_viewportSize.y);
  ImGui::Text(" samples: %i", m_frameSamples);

  if (m_currentlyRendering)
    ImGui::Text(" latency: %.2fms", m_latestFL);
  else
    ImGui::Text(" latency: --");

  ImGui::Text("   (min): %.2fms", m_minFL);
  ImGui::Text("   (max): %.2fms", m_maxFL);

  ImGui::Separator();

  static bool showCameraInfo = false;

  ImGui::Checkbox("camera info", &showCameraInfo);

  if (showCameraInfo) {
    const auto eye = m_arcball->eye();
    const auto dir = m_arcball->dir();
    const auto up = m_arcball->up();

    ImGui::Text("eye: %.2f, %.2f, %.2f", eye.x, eye.y, eye.z);
    ImGui::Text("dir: %.2f, %.2f, %.2f", dir.x, dir.y, dir.z);
    ImGui::Text(" up: %.2f, %.2f, %.2f", up.x, up.y, up.z);
  }

  ImGui::End();
}

} // namespace windows
