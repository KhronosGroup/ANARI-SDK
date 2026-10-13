// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// A GL app hosting the device, as Vela does: the app has its own GLX context
// current on the main thread. `render_app_gl device-first`:
//   1. a storm device created before the app's context renders;
//   2. the app makes its GLX context current; app GL works, and the first
//      device still renders;
//   3. a second storm device created while the app's GLX context is current
//      renders, and app GL still works.
// `render_app_gl app-first` skips step 1, so the app's GLX context is the
// process's first. That is Vela's order (window first, device later), and the
// case where, with GLVND, the device's EGL context cannot be made current (the
// app's GLX context owns the dispatch), so auto mode must fall back to a GLX
// context of its own. Needs an X display (skips without).

#include <anari/anari.h>

#include <GL/gl.h>
#include <GL/glx.h>

#include <cstdio>
#include <cstring>
#include <string>

static int g_failures = 0;
static int g_errors = 0;

static void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_WARNING || std::strstr(message, "GL context"))
    std::printf("    [status] %s\n", message);
  if (severity <= ANARI_SEVERITY_ERROR)
    ++g_errors;
}

static void check(bool ok, const char *what)
{
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok)
    ++g_failures;
}

struct Scene
{
  ANARIDevice dev{nullptr};
  ANARIFrame frame{nullptr};
};

// A frame whose renderer clears to a known background.
static Scene makeScene(ANARIDevice dev)
{
  Scene s;
  s.dev = dev;
  ANARIRenderer ren = anariNewRenderer(dev, "default");
  float bg[4] = {0.2f, 0.4f, 0.6f, 1.f};
  anariSetParameter(dev, ren, "background", ANARI_FLOAT32_VEC4, bg);
  anariCommitParameters(dev, ren);
  ANARICamera cam = anariNewCamera(dev, "perspective");
  anariCommitParameters(dev, cam);
  ANARIWorld world = anariNewWorld(dev);
  anariCommitParameters(dev, world);
  s.frame = anariNewFrame(dev);
  uint32_t size[2] = {32, 32};
  ANARIDataType fmt = ANARI_UFIXED8_VEC4;
  anariSetParameter(dev, s.frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(dev, s.frame, "channel.color", ANARI_DATA_TYPE, &fmt);
  anariSetParameter(dev, s.frame, "renderer", ANARI_RENDERER, &ren);
  anariSetParameter(dev, s.frame, "camera", ANARI_CAMERA, &cam);
  anariSetParameter(dev, s.frame, "world", ANARI_WORLD, &world);
  anariCommitParameters(dev, s.frame);
  anariRelease(dev, ren);
  anariRelease(dev, cam);
  anariRelease(dev, world);
  return s;
}

// Render and check the background reached the frame.
static bool renders(const Scene &s)
{
  anariRenderFrame(s.dev, s.frame);
  anariFrameReady(s.dev, s.frame, ANARI_WAIT);
  uint32_t w = 0, h = 0;
  ANARIDataType type = ANARI_UNKNOWN;
  auto *px = (const uint8_t *)anariMapFrame(
      s.dev, s.frame, "channel.color", &w, &h, &type);
  const bool ok = px && w == 32 && h == 32 && px[0] > 40 && px[0] < 60
      && px[1] > 92 && px[1] < 112 && px[2] > 143 && px[2] < 163;
  if (px)
    std::printf("    pixel (0,0) = %u %u %u\n", px[0], px[1], px[2]);
  anariUnmapFrame(s.dev, s.frame, "channel.color");
  return ok;
}

static void release(Scene &s)
{
  anariRelease(s.dev, s.frame);
  anariRelease(s.dev, s.dev);
}

// The app's own GL still works: clear its window and read the color back.
static bool appGLWorks(Display *display, GLXContext ctx)
{
  if (glXGetCurrentContext() != ctx)
    return false;
  glClearColor(0.25f, 0.5f, 0.75f, 1.f);
  glClear(GL_COLOR_BUFFER_BIT);
  float px[4] = {};
  glReadPixels(1, 1, 1, 1, GL_RGBA, GL_FLOAT, px);
  (void)display;
  return glGetError() == GL_NO_ERROR && px[0] > 0.2f && px[0] < 0.3f
      && px[1] > 0.45f && px[1] < 0.55f && px[2] > 0.7f && px[2] < 0.8f;
}

int main(int argc, char **argv)
{
  const bool deviceFirst = argc > 1 && !std::strcmp(argv[1], "device-first");
  Display *display = XOpenDisplay(nullptr);
  if (!display) {
    std::printf("SKIP: no X display for the app's GLX context\n");
    return 77;
  }

  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  if (!lib) {
    std::printf("FAIL: could not load anari library 'hydra'\n");
    return 2;
  }

  Scene s1;
  if (deviceFirst) {
    std::printf("1. device before the app's GL context:\n");
    ANARIDevice dev1 = anariNewDevice(lib, "storm");
    check(dev1 != nullptr, "storm device created");
    if (!dev1)
      return 1;
    s1 = makeScene(dev1);
    check(renders(s1), "device renders");
  }

  std::printf("2. app makes its GLX context current:\n");
  int attribs[] = {GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, 0};
  XVisualInfo *vi = glXChooseVisual(display, DefaultScreen(display), attribs);
  check(vi != nullptr, "app found a GLX visual");
  if (!vi)
    return 1;
  XSetWindowAttributes swa{};
  swa.colormap = XCreateColormap(
      display, RootWindow(display, vi->screen), vi->visual, AllocNone);
  Window win = XCreateWindow(display,
      RootWindow(display, vi->screen),
      0,
      0,
      64,
      64,
      0,
      vi->depth,
      InputOutput,
      vi->visual,
      CWColormap,
      &swa);
  GLXContext ctx = glXCreateContext(display, vi, nullptr, True);
  check(ctx && glXMakeCurrent(display, win, ctx), "app GLX context current");
  check(appGLWorks(display, ctx), "app GL works");
  if (deviceFirst)
    check(renders(s1), "first device still renders");

  std::printf("3. device created while the app's GLX context is current:\n");
  ANARIDevice dev2 = anariNewDevice(lib, "storm");
  check(dev2 != nullptr, "second storm device created");
  if (dev2) {
    Scene s2 = makeScene(dev2);
    check(renders(s2), "second device renders");
    release(s2);
  }
  check(appGLWorks(display, ctx), "app GL still works");
  if (deviceFirst) {
    check(renders(s1), "first device still renders");
    release(s1);
  }
  check(g_errors == 0, "no error messages");

  anariUnloadLibrary(lib);
  glXMakeCurrent(display, 0, nullptr);
  glXDestroyContext(display, ctx);
  XDestroyWindow(display, win);
  XFree(vi);
  XCloseDisplay(display);

  std::printf(g_failures ? "\nCHECKS FAILED (%d issues)\n"
                         : "\nALL CHECKS PASSED (%d issues)\n",
      g_failures);
  return g_failures ? 1 : 0;
}
