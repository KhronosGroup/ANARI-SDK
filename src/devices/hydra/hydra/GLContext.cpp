// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "GLContext.h"

#include <pxr/base/arch/defines.h>
#include <pxr/imaging/garch/glDebugWindow.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef ANARI_HYDRA_HAS_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

#if defined(ARCH_OS_LINUX)
#include <dlfcn.h>
// Last: Xlib defines macros (None, Bool, Status, ...) that clash with USD.
#include <X11/Xlib.h>
#undef None // clashes with GLMode::None
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace anari_hydra {

namespace {

constexpr unsigned kGLRenderer = 0x1F01; // GL_RENDERER
using GetStringFn = const unsigned char *(*)(unsigned);

std::string rendererString(GetStringFn getString)
{
  const unsigned char *s = getString ? getString(kGLRenderer) : nullptr;
  return s ? reinterpret_cast<const char *>(s) : "unknown renderer";
}

#ifdef ANARI_HYDRA_HAS_EGL

std::string eglErrorString(const char *what)
{
  char buf[96];
  std::snprintf(buf,
      sizeof(buf),
      "%s failed (EGL error 0x%04x)",
      what,
      unsigned(eglGetError()));
  return buf;
}

bool hasExtension(const char *list, const char *ext)
{
  if (!list)
    return false;
  const size_t n = std::strlen(ext);
  for (const char *p = list; (p = std::strstr(p, ext)); p += n) {
    if ((p == list || p[-1] == ' ') && (p[n] == ' ' || p[n] == '\0'))
      return true;
  }
  return false;
}

class EGLContextImpl : public GLContext
{
 public:
  ~EGLContextImpl() override
  {
    eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (m_surface != EGL_NO_SURFACE)
      eglDestroySurface(m_display, m_surface);
    if (m_context != EGL_NO_CONTEXT)
      eglDestroyContext(m_display, m_context);
    // No eglTerminate: the display is a per-process singleton that other
    // devices' workers may still be using.
  }

  // Returns null with `error` set on failure.
  static std::unique_ptr<GLContext> create(
      std::string &description, std::string &error)
  {
    const char *clientExts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (!getPlatformDisplay
        || !hasExtension(clientExts, "EGL_EXT_platform_base")) {
      error = "EGL: no EGL_EXT_platform_base (is libEGL installed?)";
      return nullptr;
    }

    long deviceIndex = 0;
    const char *deviceEnv = std::getenv("ANARI_HYDRA_EGL_DEVICE");
    const bool deviceChosen = deviceEnv && *deviceEnv;
    if (deviceChosen) {
      char *end = nullptr;
      deviceIndex = std::strtol(deviceEnv, &end, 10);
      if (*end != '\0' || deviceIndex < 0) {
        error = std::string("EGL: invalid ANARI_HYDRA_EGL_DEVICE '") + deviceEnv
            + "' (expected a device index)";
        return nullptr;
      }
    }

    // Prefer a GPU through the device platform. Fall back to Mesa's
    // surfaceless platform (e.g. llvmpipe) where there is no device, or where
    // the default device does not initialize (Mesa also lists GPUs it has no
    // driver for).
    const bool surfacelessOk =
        hasExtension(clientExts, "EGL_MESA_platform_surfaceless");
    EGLDisplay display = EGL_NO_DISPLAY;
    std::string where;
    auto queryDevices = reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(
        eglGetProcAddress("eglQueryDevicesEXT"));
    EGLint numDevices = 0;
    EGLDeviceEXT devices[32];
    if (queryDevices && hasExtension(clientExts, "EGL_EXT_platform_device")
        && queryDevices(32, devices, &numDevices) && numDevices > 0) {
      if (deviceIndex >= numDevices) {
        error = "EGL: ANARI_HYDRA_EGL_DEVICE=" + std::to_string(deviceIndex)
            + " but only " + std::to_string(numDevices) + " EGL device(s)";
        return nullptr;
      }
      display = getPlatformDisplay(
          EGL_PLATFORM_DEVICE_EXT, devices[deviceIndex], nullptr);
      where = "device " + std::to_string(deviceIndex);
      if (display == EGL_NO_DISPLAY
          || !eglInitialize(display, nullptr, nullptr)) {
        error = "EGL: " + eglErrorString("eglInitialize") + " on " + where;
        if (deviceChosen || !surfacelessOk)
          return nullptr;
        display = EGL_NO_DISPLAY;
      }
    }
    if (display == EGL_NO_DISPLAY) {
      if (!surfacelessOk) {
        error =
            "EGL: neither EGL_EXT_platform_device (with a device) nor "
            "EGL_MESA_platform_surfaceless is available";
        return nullptr;
      }
      display = getPlatformDisplay(
          EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
      where = "surfaceless";
      if (display == EGL_NO_DISPLAY
          || !eglInitialize(display, nullptr, nullptr)) {
        error = "EGL: " + eglErrorString("eglInitialize") + " on " + where;
        return nullptr;
      }
    }
    error.clear();

    if (!eglBindAPI(EGL_OPENGL_API)) {
      error = "EGL: " + eglErrorString("eglBindAPI(EGL_OPENGL_API)");
      return nullptr;
    }
    const EGLint configAttribs[] = {EGL_SURFACE_TYPE,
        EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_BIT,
        EGL_RED_SIZE,
        8,
        EGL_GREEN_SIZE,
        8,
        EGL_BLUE_SIZE,
        8,
        EGL_DEPTH_SIZE,
        24,
        EGL_NONE};
    EGLConfig config = nullptr;
    EGLint numConfigs = 0;
    if (!eglChooseConfig(display, configAttribs, &config, 1, &numConfigs)
        || numConfigs < 1) {
      error = "EGL: no pbuffer-capable OpenGL config";
      return nullptr;
    }

    auto ctx = std::unique_ptr<EGLContextImpl>(new EGLContextImpl);
    ctx->m_display = display;
    // A compatibility-profile context of the highest version, as Garch's GLX
    // window creates.
    ctx->m_context = eglCreateContext(display, config, EGL_NO_CONTEXT, nullptr);
    if (ctx->m_context == EGL_NO_CONTEXT) {
      error = "EGL: " + eglErrorString("eglCreateContext");
      return nullptr;
    }
    // A small pbuffer gives the context a default framebuffer, as the GLX
    // window does. Without one, some of Storm's draws hit an incomplete
    // framebuffer and raise GL errors.
    const EGLint pbufferAttribs[] = {EGL_WIDTH, 8, EGL_HEIGHT, 8, EGL_NONE};
    ctx->m_surface = eglCreatePbufferSurface(display, config, pbufferAttribs);
    if (ctx->m_surface == EGL_NO_SURFACE) {
      error = "EGL: " + eglErrorString("eglCreatePbufferSurface");
      return nullptr;
    }
    // With GLVND, eglMakeCurrent fails when the app already has a GLX context
    // current from the same vendor (entry-point patching), so check that the
    // context really is current; auto mode then falls back to GLX.
    if (!eglMakeCurrent(display, ctx->m_surface, ctx->m_surface, ctx->m_context)
        || eglGetCurrentContext() != ctx->m_context) {
      error = "EGL: " + eglErrorString("eglMakeCurrent")
          + " (with GLVND this happens when the app already uses GLX)";
      return nullptr;
    }

    auto getString =
        reinterpret_cast<GetStringFn>(eglGetProcAddress("glGetString"));
    description = "EGL (" + rendererString(getString) + ", " + where + ")";
    return ctx;
  }

 private:
  EGLContextImpl() = default;

  EGLDisplay m_display{EGL_NO_DISPLAY};
  EGLContext m_context{EGL_NO_CONTEXT};
  EGLSurface m_surface{EGL_NO_SURFACE};
};

#endif // ANARI_HYDRA_HAS_EGL

class GLXContextImpl : public GLContext
{
 public:
  static std::unique_ptr<GLContext> create(
      std::string &description, std::string &error)
  {
#if defined(ARCH_OS_LINUX)
    // Garch dereferences a null Display* when no X display opens; probe first.
    Display *display = XOpenDisplay(nullptr);
    if (!display) {
      error = "GLX: cannot open an X display (DISPLAY unset or unreachable)";
      return nullptr;
    }
    XCloseDisplay(display);
#endif
    auto ctx = std::unique_ptr<GLXContextImpl>(new GLXContextImpl);
    ctx->m_window =
        std::make_unique<GarchGLDebugWindow>("anari-hydra worker", 8, 8);
    ctx->m_window->Init();

    GetStringFn getString = nullptr;
#if defined(ARCH_OS_LINUX)
    getString =
        reinterpret_cast<GetStringFn>(dlsym(RTLD_DEFAULT, "glGetString"));
#endif
    description = "GLX (" + rendererString(getString) + ")";
    return ctx;
  }

 private:
  GLXContextImpl() = default;

  // A hidden 8x8 window whose context stays current on the worker thread.
  std::unique_ptr<GarchGLDebugWindow> m_window;
};

} // namespace

bool glModeFromEnv(GLMode &mode, std::string &error)
{
  mode = GLMode::Auto;
  const char *env = std::getenv("ANARI_HYDRA_GL");
  if (!env || !*env || !std::strcmp(env, "auto"))
    return true;
  if (!std::strcmp(env, "egl"))
    mode = GLMode::EGL;
  else if (!std::strcmp(env, "glx"))
    mode = GLMode::GLX;
  else if (!std::strcmp(env, "none"))
    mode = GLMode::None;
  else {
    error = std::string("invalid ANARI_HYDRA_GL '") + env
        + "' (expected auto, egl, glx or none)";
    return false;
  }
  return true;
}

std::unique_ptr<GLContext> GLContext::create(
    GLMode mode, std::string &description, std::string &error)
{
  description = "none";
  error.clear();
  std::unique_ptr<GLContext> ctx;
  std::string eglError, glxError;

  if (mode == GLMode::Auto || mode == GLMode::EGL) {
#ifdef ANARI_HYDRA_HAS_EGL
    ctx = EGLContextImpl::create(description, eglError);
#else
    eglError = "EGL: not built in (libEGL was not found at configure time)";
#endif
    if (ctx)
      return ctx;
    if (mode == GLMode::EGL) {
      error = eglError;
      return nullptr;
    }
  }

  if (mode == GLMode::Auto || mode == GLMode::GLX) {
    ctx = GLXContextImpl::create(description, glxError);
    if (ctx) {
      if (mode == GLMode::Auto) // say why EGL was passed over
        description += " [" + eglError + "]";
      return ctx;
    }
    error = mode == GLMode::GLX ? glxError : eglError + "; " + glxError;
    return nullptr;
  }

  return nullptr; // GLMode::None
}

} // namespace anari_hydra
