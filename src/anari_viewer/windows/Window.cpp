// Copyright 2021 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "Window.h"

namespace anari_viewer {

Window::Window(const char *name, bool startShown, bool wrapBeginEnd)
    : m_name(name), m_visible(startShown), m_wrapBeginEnd(wrapBeginEnd)
{}

void Window::renderUI()
{
  if (!m_visible)
    return;

  ImGui::SetNextWindowSize(ImVec2(550, 680), ImGuiCond_FirstUseEver);

  if (m_wrapBeginEnd)
    ImGui::Begin(m_name.c_str(), &m_visible, windowFlags());

  buildUI();

  if (m_wrapBeginEnd)
    ImGui::End();
}

void Window::show()
{
  m_visible = true;
}

void Window::hide()
{
  m_visible = false;
}

void Window::toggleShown()
{
  m_visible = !m_visible;
}

bool *Window::visiblePtr()
{
  return &m_visible;
}

const char *Window::name()
{
  return m_name.c_str();
}

ImGuiWindowFlags Window::windowFlags() const
{
  return 0;
}

} // namespace anari_viewer