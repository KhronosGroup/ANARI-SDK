// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// KHR_FRAME_COMPLETION_CALLBACK (SPEC §8.7): the frame's
// `frameCompletionCallback` fires exactly once per completed render, with the
// committed `frameCompletionCallbackUserData`, on the device worker thread
// after the frame is observably ready. Drives everything through the ANARI C
// API. Cross-thread synchronization uses condition variables with a generous
// hang guard, never fixed sleeps.

#include <anari/anari.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

static int g_errors = 0;
// The error-path section provokes device errors on purpose; only errors
// outside it count as failures.
static bool g_expectErrors = false;

static void statusFunc(const void *, ANARIDevice, ANARIObject,
    ANARIDataType, ANARIStatusSeverity severity, ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_WARNING)
    std::printf("[%s] %s\n",
        severity <= ANARI_SEVERITY_ERROR ? "ERROR" : "WARN", message);
  if (severity <= ANARI_SEVERITY_ERROR && !g_expectErrors)
    ++g_errors;
}

static void check(bool ok, const char *what)
{
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok)
    ++g_errors;
}

// Hang guard only: every wait below is predicate-driven and normally returns
// as soon as the callback signals.
static constexpr auto kHangGuard = std::chrono::seconds(60);
static constexpr uint32_t kSize = 64;

enum class Which
{
  NONE,
  COUNTING,
  ALT
};

// Callback-side record, shared by all callbacks in this test. Every field is
// guarded by `mutex`; read it through snapshot().
struct Record
{
  std::mutex mutex;
  std::condition_variable cv;
  int count{0};
  Which which{Which::NONE};
  const void *lastUser{nullptr};
  ANARIDevice lastDevice{nullptr};
  ANARIFrame lastFrame{nullptr};
  std::thread::id lastThread;

  // Results of API calls made from inside the probing callback.
  int readyNoWait{-1};
  int readyWait{-1};
  float duration{-1.f};
  bool mapped{false};
  bool mapNonBlack{false};
  bool depthMapped{false};

  // Other-frame probe: the frame the callback drives, and what it observed.
  ANARIFrame other{nullptr};
  bool otherMapped{false};
  bool otherMapNonBlack{false};
  int otherReady{-1};

  // The probing callback holds until the app thread is about to block on the
  // frame, so the app reliably holds the frame's object lock meanwhile.
  bool gate{false};
  bool appBlocking{false};

  // Re-render chain: the callback re-renders the frame until count reaches
  // chainTarget (0 = no chain, <0 = unbounded).
  int chainTarget{0};

  void reset()
  {
    std::lock_guard<std::mutex> lock(mutex);
    count = 0;
    which = Which::NONE;
    lastUser = nullptr;
    lastDevice = nullptr;
    lastFrame = nullptr;
    lastThread = {};
    readyNoWait = readyWait = -1;
    duration = -1.f;
    mapped = mapNonBlack = depthMapped = false;
    other = nullptr;
    otherMapped = otherMapNonBlack = false;
    otherReady = -1;
    gate = appBlocking = false;
    chainTarget = 0;
  }

  void note(Which w, const void *u, ANARIDevice d, ANARIFrame f)
  {
    {
      std::lock_guard<std::mutex> lock(mutex);
      ++count;
      which = w;
      lastUser = u;
      lastDevice = d;
      lastFrame = f;
      lastThread = std::this_thread::get_id();
    }
    cv.notify_all();
  }

  bool waitFor(int n)
  {
    std::unique_lock<std::mutex> lock(mutex);
    return cv.wait_for(lock, kHangGuard, [&] { return count >= n; });
  }

  struct Snapshot
  {
    int count;
    Which which;
    const void *lastUser;
    ANARIDevice lastDevice;
    ANARIFrame lastFrame;
    std::thread::id lastThread;
    int readyNoWait, readyWait;
    float duration;
    bool mapped, mapNonBlack, depthMapped;
  };

  Snapshot snapshot()
  {
    std::lock_guard<std::mutex> lock(mutex);
    return {count,
        which,
        lastUser,
        lastDevice,
        lastFrame,
        lastThread,
        readyNoWait,
        readyWait,
        duration,
        mapped,
        mapNonBlack,
        depthMapped};
  }
};

static Record g_rec;

// Distinct user-data targets so replacement is observable through lastUser.
static int g_tagA = 0, g_tagB = 0;

static void countingCallback(const void *u, ANARIDevice d, ANARIFrame f)
{
  g_rec.note(Which::COUNTING, u, d, f);
}

static void altCallback(const void *u, ANARIDevice d, ANARIFrame f)
{
  g_rec.note(Which::ALT, u, d, f);
}

// Exercises the callback-safe operations on the completing frame from inside
// its callback, while the app thread is blocked on the same frame.
static void probingCallback(const void *u, ANARIDevice d, ANARIFrame f)
{
  {
    std::unique_lock<std::mutex> lock(g_rec.mutex);
    if (g_rec.gate)
      g_rec.cv.wait_for(lock, kHangGuard, [] { return g_rec.appBlocking; });
  }
  const int ready = anariFrameReady(d, f, ANARI_NO_WAIT);
  const int readyWait = anariFrameReady(d, f, ANARI_WAIT);
  float dur = -1.f;
  anariGetProperty(
      d, f, "duration", ANARI_FLOAT32, &dur, sizeof(dur), ANARI_WAIT);
  uint32_t w = 0, h = 0;
  ANARIDataType type = ANARI_UNKNOWN;
  const auto *px =
      (const uint8_t *)anariMapFrame(d, f, "channel.color", &w, &h, &type);
  bool nonBlack = false;
  if (px && type == ANARI_UFIXED8_RGBA_SRGB) {
    for (uint32_t i = 0; i < w * h && !nonBlack; ++i)
      nonBlack = px[i * 4] > 32;
  }
  anariUnmapFrame(d, f, "channel.color");
  uint32_t dw = 0, dh = 0;
  const void *depth =
      anariMapFrame(d, f, "channel.depth", &dw, &dh, &type);
  anariUnmapFrame(d, f, "channel.depth");
  // Retain/release and discard of the completing frame are callback-safe too.
  anariRetain(d, f);
  anariRelease(d, f);
  anariDiscardFrame(d, f);
  {
    std::lock_guard<std::mutex> lock(g_rec.mutex);
    g_rec.readyNoWait = ready;
    g_rec.readyWait = readyWait;
    g_rec.duration = dur;
    g_rec.mapped = px != nullptr && w == kSize && h == kSize;
    g_rec.mapNonBlack = nonBlack;
    g_rec.depthMapped = depth != nullptr && dw == kSize && dh == kSize
        && type == ANARI_FLOAT32;
  }
  g_rec.note(Which::COUNTING, u, d, f);
}

static void waitForGate()
{
  std::unique_lock<std::mutex> lock(g_rec.mutex);
  if (g_rec.gate)
    g_rec.cv.wait_for(lock, kHangGuard, [] { return g_rec.appBlocking; });
}

static bool anyNonBlack(const uint8_t *px, uint32_t w, uint32_t h)
{
  for (uint32_t i = 0; i < w * h; ++i) {
    if (px[i * 4] > 32)
      return true;
  }
  return false;
}

static constexpr uint32_t kResize = 32;

// On its first call, re-configures its own frame: a new size and new user
// data, committed from inside the callback.
static void reconfiguringCallback(const void *u, ANARIDevice d, ANARIFrame f)
{
  waitForGate();
  if (u == &g_tagA) {
    uint32_t size[2] = {kResize, kResize};
    anariSetParameter(d, f, "size", ANARI_UINT32_VEC2, size);
    anariSetParameter(
        d, f, "frameCompletionCallbackUserData", ANARI_VOID_POINTER, &g_tagB);
    anariCommitParameters(d, f);
  }
  g_rec.note(Which::COUNTING, u, d, f);
}

// Drives another frame from inside this frame's callback: query and map its
// last completed render, then render it again.
static void otherFrameCallback(const void *u, ANARIDevice d, ANARIFrame f)
{
  waitForGate();
  ANARIFrame other = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_rec.mutex);
    other = g_rec.other;
  }
  const int ready = anariFrameReady(d, other, ANARI_NO_WAIT);
  uint32_t w = 0, h = 0;
  ANARIDataType type = ANARI_UNKNOWN;
  const auto *px =
      (const uint8_t *)anariMapFrame(d, other, "channel.color", &w, &h, &type);
  const bool mapped = px != nullptr && w == kSize && h == kSize
      && type == ANARI_UFIXED8_RGBA_SRGB;
  const bool nonBlack = mapped && anyNonBlack(px, w, h);
  anariUnmapFrame(d, other, "channel.color");
  anariRenderFrame(d, other);
  {
    std::lock_guard<std::mutex> lock(g_rec.mutex);
    g_rec.otherReady = ready;
    g_rec.otherMapped = mapped;
    g_rec.otherMapNonBlack = nonBlack;
  }
  g_rec.note(Which::COUNTING, u, d, f);
}

// Re-renders the completing frame from inside its own callback.
static void chainingCallback(const void *u, ANARIDevice d, ANARIFrame f)
{
  bool again = false;
  {
    std::lock_guard<std::mutex> lock(g_rec.mutex);
    again = g_rec.chainTarget < 0 || g_rec.count + 1 < g_rec.chainTarget;
  }
  if (again)
    anariRenderFrame(d, f);
  g_rec.note(Which::COUNTING, u, d, f);
}

static void setCallback(ANARIDevice dev,
    ANARIFrame frame,
    ANARIFrameCompletionCallback cb,
    const void *user)
{
  anariSetParameter(dev,
      frame,
      "frameCompletionCallback",
      ANARI_FRAME_COMPLETION_CALLBACK,
      &cb);
  // ANARI_VOID_POINTER parameters pass the pointer value itself as `mem`.
  anariSetParameter(
      dev, frame, "frameCompletionCallbackUserData", ANARI_VOID_POINTER, user);
  anariCommitParameters(dev, frame);
}

struct Scene
{
  ANARIDevice dev{nullptr};
  ANARIWorld world{nullptr};
  ANARICamera cam{nullptr};
  ANARIRenderer ren{nullptr};

  void build(ANARIDevice d)
  {
    dev = d;
    float verts[9] = {-0.8f, -0.8f, 0.f, 0.8f, -0.8f, 0.f, 0.f, 0.9f, 0.f};
    ANARIArray1D pos =
        anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 3);
    ANARIGeometry geom = anariNewGeometry(dev, "triangle");
    anariSetParameter(dev, geom, "vertex.position", ANARI_ARRAY1D, &pos);
    anariCommitParameters(dev, geom);
    ANARIMaterial mat = anariNewMaterial(dev, "matte");
    float color[3] = {0.9f, 0.9f, 0.9f};
    anariSetParameter(dev, mat, "color", ANARI_FLOAT32_VEC3, color);
    anariCommitParameters(dev, mat);
    ANARISurface surf = anariNewSurface(dev);
    anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &geom);
    anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
    anariCommitParameters(dev, surf);
    world = anariNewWorld(dev);
    ANARIArray1D surfs =
        anariNewArray1D(dev, &surf, nullptr, nullptr, ANARI_SURFACE, 1);
    anariSetParameter(dev, world, "surface", ANARI_ARRAY1D, &surfs);
    anariCommitParameters(dev, world);
    anariRelease(dev, pos);
    anariRelease(dev, geom);
    anariRelease(dev, mat);
    anariRelease(dev, surf);
    anariRelease(dev, surfs);

    cam = anariNewCamera(dev, "perspective");
    float eye[3] = {0.f, 0.f, 3.f}, dir[3] = {0.f, 0.f, -1.f},
          up[3] = {0.f, 1.f, 0.f};
    anariSetParameter(dev, cam, "position", ANARI_FLOAT32_VEC3, eye);
    anariSetParameter(dev, cam, "direction", ANARI_FLOAT32_VEC3, dir);
    anariSetParameter(dev, cam, "up", ANARI_FLOAT32_VEC3, up);
    anariCommitParameters(dev, cam);
    ren = anariNewRenderer(dev, "default");
    anariCommitParameters(dev, ren);
  }

  ANARIFrame newFrame()
  {
    ANARIFrame f = anariNewFrame(dev);
    uint32_t size[2] = {kSize, kSize};
    ANARIDataType fmt = ANARI_UFIXED8_RGBA_SRGB;
    ANARIDataType depthFmt = ANARI_FLOAT32;
    anariSetParameter(dev, f, "size", ANARI_UINT32_VEC2, size);
    anariSetParameter(dev, f, "channel.color", ANARI_DATA_TYPE, &fmt);
    anariSetParameter(dev, f, "channel.depth", ANARI_DATA_TYPE, &depthFmt);
    anariSetParameter(dev, f, "world", ANARI_WORLD, &world);
    anariSetParameter(dev, f, "camera", ANARI_CAMERA, &cam);
    anariSetParameter(dev, f, "renderer", ANARI_RENDERER, &ren);
    anariCommitParameters(dev, f);
    return f;
  }

  void release()
  {
    anariRelease(dev, world);
    anariRelease(dev, cam);
    anariRelease(dev, ren);
  }
};

// Arm the probing callback's gate, then report the app is about to block.
static void armGate()
{
  std::lock_guard<std::mutex> lock(g_rec.mutex);
  g_rec.gate = true;
}

static void openGate()
{
  {
    std::lock_guard<std::mutex> lock(g_rec.mutex);
    g_rec.appBlocking = true;
  }
  g_rec.cv.notify_all();
}

static void checkProbes(const char *how)
{
  const auto s = g_rec.snapshot();
  std::printf("  (%s)\n", how);
  check(s.count == 1, "probing callback fired once");
  check(s.readyNoWait == 1, "frame is ready (NO_WAIT) inside callback");
  check(s.readyWait == 1, "WAIT inside callback returns without deadlock");
  check(s.duration > 0.f, "duration is readable inside callback");
  check(s.mapped, "color channel maps inside callback");
  check(s.mapNonBlack, "mapped color holds the completed render");
  check(s.depthMapped, "depth AOV maps inside callback");
}

int main(int argc, char **argv)
{
  const char *subtype = argc > 1 ? argv[1] : "storm";
  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  if (!lib) {
    std::printf("FAIL: could not load anari library 'hydra'\n");
    return 2;
  }

  // --- the extension is advertised ------------------------------------------
  {
    const char **exts = anariGetDeviceExtensions(lib, subtype);
    bool found = false;
    for (int i = 0; exts && exts[i]; ++i)
      found |= std::strcmp(exts[i], "ANARI_KHR_FRAME_COMPLETION_CALLBACK") == 0;
    check(found, "library advertises ANARI_KHR_FRAME_COMPLETION_CALLBACK");
  }

  ANARIDevice dev = anariNewDevice(lib, subtype);
  if (!dev) {
    std::printf("FAIL: no device\n");
    return 2;
  }
  Scene scene;
  scene.build(dev);

  // --- count and user data across repeated renders --------------------------
  {
    g_rec.reset();
    ANARIFrame frame = scene.newFrame();
    setCallback(dev, frame, countingCallback, &g_tagA);
    const auto app = std::this_thread::get_id();
    constexpr int N = 5;
    for (int i = 0; i < N; ++i) {
      anariRenderFrame(dev, frame);
      anariFrameReady(dev, frame, ANARI_WAIT);
      // Completion happens before the wait returns (synchronous with WAIT).
      check(g_rec.snapshot().count == i + 1,
          "one callback per render, before WAIT returns");
    }
    auto s = g_rec.snapshot();
    check(s.lastUser == &g_tagA, "callback receives committed user data");
    check(s.lastDevice == dev, "callback receives the device handle");
    check(s.lastFrame == frame, "callback receives the frame handle");
    check(s.lastThread != std::thread::id() && s.lastThread != app,
        "callback runs off the app thread");

    // Back-to-back renders without waiting in between: still one each.
    g_rec.reset();
    for (int i = 0; i < N; ++i)
      anariRenderFrame(dev, frame);
    check(g_rec.waitFor(N), "queued renders all complete via callback");
    anariFrameReady(dev, frame, ANARI_WAIT);
    check(g_rec.snapshot().count == N, "queued renders fire exactly N callbacks");

    // Replacement: new callback and user data take effect on the next render.
    g_rec.reset();
    setCallback(dev, frame, altCallback, &g_tagB);
    anariRenderFrame(dev, frame);
    anariFrameReady(dev, frame, ANARI_WAIT);
    s = g_rec.snapshot();
    check(s.count == 1 && s.which == Which::ALT && s.lastUser == &g_tagB,
        "replaced callback and user data are used");

    // Removal: unsetting the callback stops invocations.
    g_rec.reset();
    anariUnsetParameter(dev, frame, "frameCompletionCallback");
    anariCommitParameters(dev, frame);
    anariRenderFrame(dev, frame);
    anariFrameReady(dev, frame, ANARI_WAIT);
    check(g_rec.snapshot().count == 0, "removed callback is not invoked");

    // Null user data is passed through as null.
    g_rec.reset();
    setCallback(dev, frame, countingCallback, nullptr);
    anariRenderFrame(dev, frame);
    anariFrameReady(dev, frame, ANARI_WAIT);
    s = g_rec.snapshot();
    check(s.count == 1 && s.lastUser == nullptr,
        "absent user data arrives as null");
    anariRelease(dev, frame);
  }

  // --- ready state and callback-safe API calls from inside the callback -----
  // The callback holds until the app thread is about to block on the same
  // frame (in WAIT, then in map), so the app holds the frame's object lock
  // while the callback maps, queries, waits on, retains/releases and discards
  // it: a reentrancy deadlock here would hang (caught by the ctest timeout).
  {
    ANARIFrame frame = scene.newFrame();
    setCallback(dev, frame, probingCallback, &g_tagA);

    g_rec.reset();
    armGate();
    anariRenderFrame(dev, frame);
    openGate();
    anariFrameReady(dev, frame, ANARI_WAIT);
    checkProbes("app blocked in anariFrameReady(WAIT)");

    g_rec.reset();
    armGate();
    anariRenderFrame(dev, frame);
    openGate();
    uint32_t w, h;
    ANARIDataType type;
    const void *depth =
        anariMapFrame(dev, frame, "channel.depth", &w, &h, &type);
    check(depth != nullptr, "app-side depth map alongside the callback's");
    anariUnmapFrame(dev, frame, "channel.depth");
    checkProbes("app blocked in anariMapFrame(channel.depth)");

    // The app does not wait at all: it learns of completion only through the
    // callback.
    g_rec.reset();
    anariRenderFrame(dev, frame);
    check(g_rec.waitFor(1), "callback signals completion without app WAIT");
    check(anariFrameReady(dev, frame, ANARI_NO_WAIT) == 1,
        "frame reports ready after the callback signalled");
    anariFrameReady(dev, frame, ANARI_WAIT);
    anariRelease(dev, frame);
  }

  // --- re-rendering from inside the callback --------------------------------
  {
    g_rec.reset();
    ANARIFrame frame = scene.newFrame();
    {
      std::lock_guard<std::mutex> lock(g_rec.mutex);
      g_rec.chainTarget = 4;
    }
    setCallback(dev, frame, chainingCallback, &g_tagA);
    anariRenderFrame(dev, frame);
    check(g_rec.waitFor(4), "callback-driven re-render chain completes");
    anariFrameReady(dev, frame, ANARI_WAIT);
    check(g_rec.snapshot().count == 4,
        "chain fires exactly one callback per render");
    anariRelease(dev, frame);
  }

  // --- setting and committing the frame's own parameters from its callback --
  // The app is blocked in WAIT on the frame meanwhile; the next render uses
  // the new size and user data.
  {
    g_rec.reset();
    ANARIFrame frame = scene.newFrame();
    setCallback(dev, frame, reconfiguringCallback, &g_tagA);
    armGate();
    anariRenderFrame(dev, frame);
    openGate();
    anariFrameReady(dev, frame, ANARI_WAIT);
    check(g_rec.snapshot().lastUser == &g_tagA,
        "reconfiguring callback fired with the original user data");

    g_rec.reset();
    anariRenderFrame(dev, frame);
    anariFrameReady(dev, frame, ANARI_WAIT);
    check(g_rec.snapshot().lastUser == &g_tagB,
        "user data committed from inside the callback is used next render");
    uint32_t w = 0, h = 0;
    ANARIDataType type = ANARI_UNKNOWN;
    const auto *px = (const uint8_t *)anariMapFrame(
        dev, frame, "channel.color", &w, &h, &type);
    check(px && w == kResize && h == kResize,
        "size committed from inside the callback is used next render");
    check(px && anyNonBlack(px, w, h), "resized render holds the scene");
    anariUnmapFrame(dev, frame, "channel.color");
    anariRelease(dev, frame);
  }

  // --- calls on another frame from inside a callback -------------------------
  // Frame `b` has a completed render; `a`'s callback queries, maps, and
  // re-renders it while the app is blocked in WAIT on `a`. `b`'s own callback
  // then fires for that render.
  {
    g_rec.reset();
    ANARIFrame a = scene.newFrame();
    ANARIFrame b = scene.newFrame();
    anariRenderFrame(dev, b);
    anariFrameReady(dev, b, ANARI_WAIT);
    setCallback(dev, a, otherFrameCallback, &g_tagA);
    setCallback(dev, b, countingCallback, &g_tagB);
    {
      std::lock_guard<std::mutex> lock(g_rec.mutex);
      g_rec.other = b;
    }
    armGate();
    anariRenderFrame(dev, a);
    openGate();
    anariFrameReady(dev, a, ANARI_WAIT);
    check(g_rec.waitFor(2), "other frame rendered from a callback completes");
    anariFrameReady(dev, b, ANARI_WAIT);
    const auto s = g_rec.snapshot();
    check(s.count == 2 && s.lastFrame == b && s.lastUser == &g_tagB,
        "other frame's own callback fires for the callback-driven render");
    {
      std::lock_guard<std::mutex> lock(g_rec.mutex);
      check(g_rec.otherReady == 1, "other frame reports ready inside callback");
      check(g_rec.otherMapped, "other frame maps inside callback");
      check(g_rec.otherMapNonBlack,
          "other frame's map holds its last completed render");
    }
    anariRelease(dev, a);
    anariRelease(dev, b);
  }

  // --- discard: v1 lets the in-flight render finish, so it still completes ---
  {
    g_rec.reset();
    ANARIFrame frame = scene.newFrame();
    setCallback(dev, frame, countingCallback, &g_tagA);
    anariRenderFrame(dev, frame);
    anariDiscardFrame(dev, frame);
    anariFrameReady(dev, frame, ANARI_WAIT);
    check(g_rec.snapshot().count == 1,
        "discarded render still completes exactly once");
    anariRelease(dev, frame);
  }

  // --- frame released while its render is pending ---------------------------
  {
    g_rec.reset();
    ANARIFrame frame = scene.newFrame();
    setCallback(dev, frame, countingCallback, &g_tagA);
    anariRenderFrame(dev, frame);
    anariRelease(dev, frame); // the queued render keeps it alive
    check(g_rec.waitFor(1), "released frame's pending render still completes");
    check(g_rec.snapshot().lastFrame == frame,
        "callback still names the released frame");
  }

  // --- concurrency: two app threads drive two frames at once ----------------
  {
    g_rec.reset();
    ANARIFrame a = scene.newFrame();
    ANARIFrame b = scene.newFrame();
    setCallback(dev, a, countingCallback, &g_tagA);
    setCallback(dev, b, countingCallback, &g_tagB);
    constexpr int N = 4;
    auto drive = [&](ANARIFrame f) {
      for (int i = 0; i < N; ++i) {
        anariRenderFrame(dev, f);
        anariFrameReady(dev, f, ANARI_WAIT);
      }
    };
    std::thread ta(drive, a), tb(drive, b);
    ta.join();
    tb.join();
    check(g_rec.snapshot().count == 2 * N,
        "concurrent frames fire one callback per render");
    anariRelease(dev, a);
    anariRelease(dev, b);
  }

  scene.release();

  // --- device released with a render still pending --------------------------
  // Pending renders complete (and call back) during anariRelease(device); no
  // callback fires after it returns.
  {
    g_rec.reset();
    ANARIDevice dev2 = anariNewDevice(lib, subtype);
    Scene s2;
    s2.build(dev2);
    ANARIFrame f = s2.newFrame();
    setCallback(dev2, f, countingCallback, &g_tagA);
    anariRenderFrame(dev2, f);
    anariRelease(dev2, f);
    s2.release();
    anariRelease(dev2, dev2);
    check(g_rec.snapshot().count == 1,
        "pending render completes exactly once during device release");
  }

  // --- device released under an unbounded re-render chain ------------------
  // The callback re-renders forever; releasing the device must still return,
  // dropping renders requested during release.
  {
    g_rec.reset();
    {
      std::lock_guard<std::mutex> lock(g_rec.mutex);
      g_rec.chainTarget = -1;
    }
    ANARIDevice dev3 = anariNewDevice(lib, subtype);
    Scene s3;
    s3.build(dev3);
    ANARIFrame f = s3.newFrame();
    setCallback(dev3, f, chainingCallback, &g_tagA);
    anariRenderFrame(dev3, f);
    check(g_rec.waitFor(3), "unbounded chain is running");
    anariRelease(dev3, f);
    s3.release();
    anariRelease(dev3, dev3);
    const int after = g_rec.snapshot().count;
    check(after >= 3, "device release returns under an unbounded chain");
  }

  // --- error path: a render that cannot run still completes -----------------
  // An unknown device subtype binds no delegate; the render job fails, but the
  // frame must still become ready and call back so waiters never hang.
  {
    g_rec.reset();
    g_expectErrors = true;
    ANARIDevice bad = anariNewDevice(lib, "no-such-delegate");
    check(bad != nullptr, "device with an unknown subtype is still created");
    if (bad) {
      ANARIFrame f = anariNewFrame(bad);
      setCallback(bad, f, countingCallback, &g_tagA);
      anariRenderFrame(bad, f);
      check(g_rec.waitFor(1), "failed render still calls back");
      check(anariFrameReady(bad, f, ANARI_NO_WAIT) == 1,
          "failed render still reports ready");
      anariRelease(bad, f);
      anariRelease(bad, bad);
    }
    g_expectErrors = false;
  }

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);

  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
      g_errors);
  return g_errors == 0 ? 0 : 1;
}
