// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The device worker's OpenGL context. HgiGL (Storm) binds to whatever context
// is current when it is created, so the worker makes one current on its own
// thread first. Selection, in `auto` mode:
//   1. EGL: an EGL_EXT_platform_device display (device ANARI_HYDRA_EGL_DEVICE,
//      default 0), or EGL_MESA_platform_surfaceless when no device platform is
//      available, with a small pbuffer as the default framebuffer. Needs no
//      display server.
//   2. GLX: a hidden GarchGLDebugWindow, if an X display opens.
//   3. none: no context. Delegates that need GL (Storm) are then unsupported;
//      CPU delegates (Embree) render without an Hgi.
// ANARI_HYDRA_GL=auto|egl|glx|none forces a mode; a forced mode that is
// unavailable is an error rather than a fallback.

#pragma once

#include <memory>
#include <string>

namespace anari_hydra {

enum class GLMode
{
  Auto,
  EGL,
  GLX,
  None
};

// Read ANARI_HYDRA_GL (unset or empty: Auto). Returns false, with `error` set,
// for an unrecognized value.
bool glModeFromEnv(GLMode &mode, std::string &error);

class GLContext
{
 public:
  virtual ~GLContext() = default;

  // Create a context per `mode` and make it current on the calling thread.
  // Returns null when no context could be made: always for None, and with
  // `error` describing why for the other modes (for Auto, why each attempt
  // failed). `description` names the context made, e.g. "EGL (<GL renderer>,
  // device 0)"; an auto-mode GLX context also says why EGL was passed over.
  static std::unique_ptr<GLContext> create(
      GLMode mode, std::string &description, std::string &error);
};

} // namespace anari_hydra
