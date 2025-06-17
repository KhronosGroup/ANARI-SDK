// Copyright 2021 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#pragma once

// imgui
#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#include <imgui.h>
// std
#include <memory>
#include <string>
#include <vector>

#include "../Application.h"

namespace anari_viewer::windows {

struct Window
{
  Window(Application *app,
      const char *name,
      bool startShown = false,
      bool wrapBeginEnd = true);
  virtual ~Window() = default;

  void renderUI();

  void show();
  void hide();
  void toggleShown();

  bool *visiblePtr();
  const char *name();

  virtual int windowFlags() const;

 protected:
  virtual void buildUI() = 0;
  Application *const m_app;

 private:
  std::string m_name;
  bool m_visible{false};
  bool m_wrapBeginEnd{true};
};

} // namespace anari_viewer::windows
