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

Viewport::Viewport(anari::Device device, const char *name)
    : Window(name, true), m_device(device)
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

  const char **r_subtypes = anariGetObjectSubtypes(m_device, ANARI_RENDERER);

  if (r_subtypes != nullptr) {
    for (int i = 0; r_subtypes[i] != nullptr; i++) {
      std::string subtype = r_subtypes[i];
      auto parameters =
          ui::parseParameters(m_device, ANARI_RENDERER, subtype.c_str());
      m_rendererNames.push_back(subtype);
      m_rendererParameters.push_back(parameters);
    }
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
  anari::math::int2 viewportSize(_viewportSize.x, _viewportSize.y);

  if (m_viewportSize != viewportSize)
    reshape(viewportSize);

  updateImage();
  updateCamera();

  ImGui::Image((void *)(intptr_t)m_framebufferTexture,
      ImGui::GetContentRegionAvail(),
      ImVec2(1, 0),
      ImVec2(0, 1));

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
  anari::math::float3 bounds[2];

  anariGetProperty(m_device,
      m_world,
      "bounds",
      ANARI_FLOAT32_BOX3,
      &bounds[0],
      sizeof(bounds),
      ANARI_WAIT);

  auto center = 0.5f * (bounds[0] + bounds[1]);
  auto diag = bounds[1] - bounds[0];

  auto azel = resetAzEl ? anari::math::float2(180.f, 200.f) : m_arcball->azel();
  m_arcball->setConfig(center, 1.25f * linalg::length(diag), azel);
  m_cameraToken = 0;
}

anari::Device Viewport::device() const
{
  return m_device;
}

void Viewport::reshape(anari::math::int2 newSize)
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
  anari::render(m_device, m_frame);
  m_renderSize = m_viewportSize;
  m_currentlyRendering = true;
  m_frameCancelled = false;
}

void Viewport::updateFrame()
{
  anari::setParameter(m_device, m_frame, "size", anari::math::uint2(m_viewportSize));
  anari::setParameter(
      m_device, m_frame, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  anari::setParameter(m_device, m_frame, "accumulation", true);
  anari::setParameter(m_device, m_frame, "world", m_world);
  if (m_useOrthoCamera)
    anari::setParameter(m_device, m_frame, "camera", m_orthoCamera);
  else
    anari::setParameter(m_device, m_frame, "camera", m_perspCamera);
  anari::setParameter(
      m_device, m_frame, "renderer", m_renderers[m_currentRenderer]);

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
  anari::setParameter(
      m_device, m_orthoCamera, "height", m_arcball->distance() * 0.75f);

  anari::setParameter(m_device,
      m_perspCamera,
      "aspect",
      m_viewportSize.x / float(m_viewportSize.y));
  anari::setParameter(m_device,
      m_orthoCamera,
      "aspect",
      m_viewportSize.x / float(m_viewportSize.y));

  auto radians = [](float degrees) -> float { return degrees * M_PI / 180.f; };
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
    m_previousMouse = anari::math::float2(-1);
  } else if (ImGui::IsItemHovered() && !m_manipulating)
    m_manipulating = true;

  if (m_mouseRotating && !leftDown)
    m_mouseRotating = false;

  if (m_manipulating) {
    anari::math::float2 position;
    std::memcpy(&position, &io.MousePos, sizeof(position));

    const anari::math::float2 mouse(position.x, position.y);

    if (anyDown && m_previousMouse != anari::math::float2(-1)) {
      const anari::math::float2 prev = m_previousMouse;

      const anari::math::float2 mouseFrom =
          prev * 2.f / anari::math::float2(m_viewportSize);
      const anari::math::float2 mouseTo = mouse * 2.f / anari::math::float2(m_viewportSize);

      const anari::math::float2 mouseDelta = mouseTo - mouseFrom;

      if (mouseDelta != anari::math::float2(0.f)) {
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
  constexpr float INDENT_AMOUNT = 25.f;

  const bool rightClicked = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Right);

  if (rightClicked && ImGui::IsWindowHovered()) {
    m_contextMenuVisible = true;
    ImGui::OpenPopup(m_contextMenuName.c_str());
  }

  if (ImGui::BeginPopup(m_contextMenuName.c_str())) {
    ImGui::Text("Renderer:");
    ImGui::Indent(INDENT_AMOUNT);

    if (m_renderers.size() > 1 && ImGui::BeginMenu("subtype")) {
      for (int i = 0; i < m_rendererNames.size(); i++) {
        if (ImGui::MenuItem(m_rendererNames[i].c_str())) {
          m_currentRenderer = i;
          updateFrame();
        }
      }
      ImGui::EndMenu();
    }

    if (!m_rendererParameters.empty() && ImGui::BeginMenu("parameters")) {
      auto &parameters = m_rendererParameters[m_currentRenderer];
      auto renderer = m_renderers[m_currentRenderer];
      for (auto &p : parameters)
        ui::buildUI(m_device, renderer, p);
      ImGui::EndMenu();
    }

    ImGui::Unindent(INDENT_AMOUNT);
    ImGui::Separator();

    ImGui::Text("Camera:");
    ImGui::Indent(INDENT_AMOUNT);

    if (ImGui::Checkbox("orthographic", &m_useOrthoCamera))
      updateFrame();

    ImGui::BeginDisabled(m_useOrthoCamera);

    if (ImGui::SliderFloat("fov", &m_fov, 0.1f, 180.f))
      updateCamera(true);

    ImGui::EndDisabled();

    if (ImGui::Combo("up", &m_arcballUp, "+x\0+y\0+z\0-x\0-y\0-z\0\0")) {
      m_arcball->setAxis(static_cast<manipulators::OrbitAxis>(m_arcballUp));
      resetView();
    }

    if (ImGui::MenuItem("reset view"))
      resetView();

    ImGui::Unindent(INDENT_AMOUNT);
    ImGui::Separator();

    ImGui::Text("Viewport:");
    ImGui::Indent(INDENT_AMOUNT);

    ImGui::Checkbox("show stats", &m_showOverlay);
    if (ImGui::MenuItem("reset stats")) {
      m_minFL = m_latestFL;
      m_maxFL = m_latestFL;
    }

    if (ImGui::MenuItem("take screenshot"))
      m_saveNextFrame = true;

    ImGui::Unindent(INDENT_AMOUNT);
    ImGui::Separator();

    ImGui::Text("World:");
    ImGui::Indent(INDENT_AMOUNT);

    if (ImGui::MenuItem("print bounds")) {
      anari::math::float3 bounds[2];

      anariGetProperty(m_device,
          m_world,
          "bounds",
          ANARI_FLOAT32_BOX3,
          &bounds[0],
          sizeof(bounds),
          ANARI_WAIT);

      printf("current world bounds {%f, %f, %f} x {%f, %f, %f}\n",
          bounds[0].x,
          bounds[0].y,
          bounds[0].z,
          bounds[1].x,
          bounds[1].y,
          bounds[1].z);
    }

    ImGui::Unindent(INDENT_AMOUNT);

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
    const auto azel = m_arcball->azel();
    const auto dist = m_arcball->distance();
    ImGui::Text("  az: %f", azel.x);
    ImGui::Text("  el: %f", azel.y);
    ImGui::Text("dist: %f", dist);
  }

  ImGui::End();
}

} // namespace windows
