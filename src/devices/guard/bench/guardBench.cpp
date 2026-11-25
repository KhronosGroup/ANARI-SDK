// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// guardBench: what the guard device costs, against the same device loaded
// in-process.
//
//   guardBench [out=overhead.md] [devices=helide,sink] [only=loop,...]
//              [runs=5] [quick=1]
//
// runs every workload both ways and writes the cost table. Each measurement
// is taken in a process of its own, which is this program again:
//
//   guardBench run workload=<name> device=<library> guard=<0|1> ...
//
// prints "key value" lines for one run of one workload. Not run by ctest: the
// figures only mean something on a machine that is doing nothing else.

#include "anari_test_scenes.h"

#include <anari/anari.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace {

using Arguments = std::map<std::string, std::string>;

Arguments parse(int argc, char *argv[], int first)
{
  Arguments a;
  for (int i = first; i < argc; i++) {
    const std::string s = argv[i];
    const auto eq = s.find('=');
    if (eq == std::string::npos) {
      std::fprintf(
          stderr, "guardBench: expected key=value, got '%s'\n", argv[i]);
      std::exit(2);
    }
    a[s.substr(0, eq)] = s.substr(eq + 1);
  }
  return a;
}

std::string text(const Arguments &a, const char *key, const char *fallback)
{
  auto it = a.find(key);
  return it == a.end() ? fallback : it->second;
}

long number(const Arguments &a, const char *key, long fallback)
{
  auto it = a.find(key);
  return it == a.end() ? fallback : std::atol(it->second.c_str());
}

double now()
{
  return std::chrono::duration<double>(
      std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// 'fraction' of the way through the values, which are sorted here.
double percentile(std::vector<double> values, double fraction)
{
  if (values.empty())
    return std::nan("");
  std::sort(values.begin(), values.end());
  const size_t i = std::min(
      values.size() - 1, size_t(std::ceil(fraction * values.size())) - 1);
  return values[i];
}

double median(const std::vector<double> &values)
{
  return percentile(values, 0.5);
}

// Other processes, seen through /proc ////////////////////////////////////////

// The pid of a process named 'name' whose parent is 'parent'; -1 if none.
pid_t findChild(pid_t parent, const std::string &name)
{
  pid_t found = -1;
  DIR *proc = opendir("/proc");
  if (!proc)
    return -1;
  while (dirent *entry = readdir(proc)) {
    char *end = nullptr;
    const long pid = std::strtol(entry->d_name, &end, 10);
    if (*end != '\0' || pid <= 0)
      continue;
    std::ifstream stat("/proc/" + std::string(entry->d_name) + "/stat");
    std::string line;
    std::getline(stat, line);
    const auto open = line.find('(');
    const auto close = line.rfind(')');
    if (open == std::string::npos || close == std::string::npos)
      continue;
    char state = 0;
    long ppid = -1;
    if (std::sscanf(line.c_str() + close + 1, " %c %ld", &state, &ppid) != 2)
      continue;
    if (ppid == parent && state != 'Z'
        && line.substr(open + 1, close - open - 1) == name) {
      found = pid_t(pid);
      break;
    }
  }
  closedir(proc);
  return found;
}

// Proportional set size: each page counts for one over the number of
// processes that have it mapped, so a region shared by the application and
// the host counts once between them.
double pssBytes(pid_t pid)
{
  std::ifstream rollup("/proc/" + std::to_string(pid) + "/smaps_rollup");
  std::string line;
  while (std::getline(rollup, line)) {
    long kb = 0;
    if (std::sscanf(line.c_str(), "Pss: %ld kB", &kb) == 1)
      return double(kb) * 1024.0;
  }
  return std::nan("");
}

// User plus system time of every thread of 'pid' so far, in clock ticks.
double cpuSeconds(pid_t pid)
{
  std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
  std::string line;
  std::getline(stat, line);
  const auto close = line.rfind(')');
  if (close == std::string::npos)
    return std::nan("");
  std::istringstream fields(line.substr(close + 1));
  std::string field;
  double ticks = 0;
  // After the name: state is field 3, utime and stime are 14 and 15.
  for (int i = 3; i <= 15 && (fields >> field); i++) {
    if (i >= 14)
      ticks += std::atof(field.c_str());
  }
  return ticks / double(sysconf(_SC_CLK_TCK));
}

int regionsMapped(pid_t pid)
{
  std::ifstream maps("/proc/" + std::to_string(pid) + "/maps");
  int count = 0;
  std::string line;
  while (std::getline(maps, line)) {
    if (line.find("memfd:anari-guard-region") != std::string::npos)
      count++;
  }
  return count;
}

// One run of one workload ////////////////////////////////////////////////////

std::atomic<int> g_errors{0};

void onStatus(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_ERROR) {
    g_errors++;
    std::fprintf(stderr, "[error] %s\n", message);
  }
}

void fail(const char *what)
{
  std::fprintf(stderr, "guardBench: %s\n", what);
  std::exit(1);
}

void out(const char *key, double value)
{
  std::printf("%s %.9g\n", key, value);
}

struct Traffic
{
  uint64_t bytesSent{0};
  uint64_t bytesReceived{0};
  uint64_t flushes{0};
  uint64_t regionsCreated{0};
};

// A device, loaded in-process or behind the guard device.
struct Session
{
  explicit Session(const Arguments &a)
      : wrapped(text(a, "device", "helide")), guarded(number(a, "guard", 0))
  {
    if (guarded)
      setenv("ANARI_GUARD_WRAPPED_DEVICE", wrapped.c_str(), 1);
    library = anariLoadLibrary(
        guarded ? "guard" : wrapped.c_str(), onStatus, nullptr);
    if (!library)
      fail("cannot load the library");
    newDevice();
  }

  void newDevice()
  {
    device = anariNewDevice(library, "default");
    if (!device)
      fail("cannot create the device");
    host = guarded ? findChild(getpid(), "anariGuardHost") : -1;
    if (guarded && host < 0)
      fail("the guard device has no host process");
  }

  // The adapter's counters; all zero in-process, where there are none.
  Traffic traffic() const
  {
    Traffic t;
    auto read = [&](const char *name, uint64_t &value) {
      anariGetProperty(device,
          device,
          name,
          ANARI_UINT64,
          &value,
          sizeof(value),
          ANARI_NO_WAIT);
    };
    if (guarded) {
      read("guard.bytesSent", t.bytesSent);
      read("guard.bytesReceived", t.bytesReceived);
      read("guard.flushes", t.flushes);
      read("guard.regionsCreated", t.regionsCreated);
    }
    return t;
  }

  void reportTraffic(const Traffic &before, double per = 1.0) const
  {
    const Traffic after = traffic();
    out("bytesSent", double(after.bytesSent - before.bytesSent) / per);
    out("bytesReceived",
        double(after.bytesReceived - before.bytesReceived) / per);
    out("flushes", double(after.flushes - before.flushes) / per);
    out("regionsCreated", double(after.regionsCreated));
  }

  // A value-returning call: through the guard device it returns once the
  // host has executed everything sent before it.
  void sync() const
  {
    int32_t version = 0;
    anariGetProperty(device,
        device,
        "version",
        ANARI_INT32,
        &version,
        sizeof(version),
        ANARI_WAIT);
  }

  // Taken while everything is still alive, so regions are still mapped.
  void reportMemory() const
  {
    const double app = pssBytes(getpid());
    const double hostPss = guarded ? pssBytes(host) : 0.0;
    out("pssApp", app);
    out("pssHost", hostPss);
    out("pss", app + hostPss);
    out("regionsMapped", regionsMapped(getpid()));
  }

  double cpu() const
  {
    rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return double(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec)
        + 1e-6 * double(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec);
  }

  double hostCpu() const
  {
    return guarded ? cpuSeconds(host) : 0.0;
  }

  void finish()
  {
    anariRelease(device, device);
    anariUnloadLibrary(library);
    out("errors", g_errors);
    out("ok", 1);
  }

  std::string wrapped;
  bool guarded{false};
  ANARILibrary library{nullptr};
  ANARIDevice device{nullptr};
  pid_t host{-1};
};

// A camera, a renderer and a frame looking at a world.
struct View
{
  View(ANARIDevice d,
      ANARIWorld world,
      const Arguments &a,
      const float *position,
      const float *direction,
      const float *up)
      : d(d),
        width(uint32_t(number(a, "width", 1920))),
        height(uint32_t(number(a, "height", 1080))),
        floatAndDepth(number(a, "float", 0))
  {
    std::memcpy(base, position, sizeof(base));
    camera = anariNewCamera(d, "perspective");
    const float aspect = float(width) / float(height);
    anariSetParameter(d, camera, "aspect", ANARI_FLOAT32, &aspect);
    anariSetParameter(d, camera, "position", ANARI_FLOAT32_VEC3, position);
    anariSetParameter(d, camera, "direction", ANARI_FLOAT32_VEC3, direction);
    anariSetParameter(d, camera, "up", ANARI_FLOAT32_VEC3, up);
    anariCommitParameters(d, camera);

    const std::string type = text(a, "renderer", "default");
    renderer = anariNewRenderer(d, type.c_str());
    const int32_t samples = int32_t(number(a, "samples", 0));
    if (samples > 0)
      anariSetParameter(d, renderer, "pixelSamples", ANARI_INT32, &samples);
    anariCommitParameters(d, renderer);

    frame = anariNewFrame(d);
    const uint32_t size[2] = {width, height};
    anariSetParameter(d, frame, "size", ANARI_UINT32_VEC2, size);
    const ANARIDataType color =
        floatAndDepth ? ANARI_FLOAT32_VEC4 : ANARI_UFIXED8_RGBA_SRGB;
    anariSetParameter(d, frame, "channel.color", ANARI_DATA_TYPE, &color);
    if (floatAndDepth) {
      const ANARIDataType depth = ANARI_FLOAT32;
      anariSetParameter(d, frame, "channel.depth", ANARI_DATA_TYPE, &depth);
    }
    anariSetParameter(d, frame, "renderer", ANARI_RENDERER, &renderer);
    anariSetParameter(d, frame, "camera", ANARI_CAMERA, &camera);
    anariSetParameter(d, frame, "world", ANARI_WORLD, &world);
    anariCommitParameters(d, frame);
  }

  // What an interactive application does to the camera between frames.
  void moveCamera(int step)
  {
    float position[3] = {base[0], base[1], base[2]};
    position[0] += 1e-3f * float(step % 2);
    anariSetParameter(d, camera, "position", ANARI_FLOAT32_VEC3, position);
    anariCommitParameters(d, camera);
  }

  void render()
  {
    anariRenderFrame(d, frame);
    if (!anariFrameReady(d, frame, ANARI_WAIT))
      fail("the frame never became ready");
  }

  void mapChannel(const char *channel)
  {
    uint32_t w = 0, h = 0;
    ANARIDataType type = ANARI_UNKNOWN;
    const void *pixels = anariMapFrame(d, frame, channel, &w, &h, &type);
    if (!pixels || w != width || h != height)
      fail("the frame did not map");
    anariUnmapFrame(d, frame, channel);
  }

  // Map and unmap what the frame was asked for. The pixels are not read:
  // an application that reads them pays for that the same either way.
  void readBack()
  {
    mapChannel("channel.color");
    if (floatAndDepth)
      mapChannel("channel.depth");
  }

  void release()
  {
    anariRelease(d, frame);
    anariRelease(d, renderer);
    anariRelease(d, camera);
  }

  ANARIDevice d;
  uint32_t width, height;
  bool floatAndDepth;
  float base[3];
  ANARICamera camera{nullptr};
  ANARIRenderer renderer{nullptr};
  ANARIFrame frame{nullptr};
};

// The SDK's 'random_spheres' test scene and a view of it.
struct SphereScene
{
  SphereScene(ANARIDevice d, const Arguments &a)
  {
    scene = anari::scenes::createScene(d, "test", "random_spheres");
    if (!scene)
      fail("the SDK has no 'random_spheres' test scene");
    anari::scenes::commit(scene);
    const auto cameras = anari::scenes::getCameras(scene);
    if (cameras.empty())
      fail("the test scene has no camera");
    const auto &c = cameras[0];
    const float position[3] = {c.position.x, c.position.y, c.position.z};
    const float direction[3] = {c.direction.x, c.direction.y, c.direction.z};
    const float up[3] = {c.up.x, c.up.y, c.up.z};
    view =
        new View(d, anari::scenes::getWorld(scene), a, position, direction, up);
  }

  void release()
  {
    view->release();
    delete view;
    anari::scenes::release(scene);
  }

  anari::scenes::SceneHandle scene{nullptr};
  View *view{nullptr};
};

// Workload 1: an interactive loop. Each frame changes and commits a camera
// parameter, renders, waits, and maps and unmaps the frame.
void interactiveLoop(const Arguments &a)
{
  const int frames = int(number(a, "frames", 1000));
  const int warmup = int(number(a, "warmup", 100));

  const double start = now();
  Session s(a);
  SphereScene scene(s.device, a);
  View &view = *scene.view;
  view.render();
  view.readBack();
  out("firstFrameSeconds", now() - start);

  for (int i = 0; i < warmup; i++) {
    view.moveCamera(i);
    view.render();
    view.readBack();
  }

  std::vector<double> frame, commit, render, readBack;
  const Traffic before = s.traffic();
  for (int i = 0; i < frames; i++) {
    const double t0 = now();
    view.moveCamera(i);
    const double t1 = now();
    view.render();
    const double t2 = now();
    view.readBack();
    const double t3 = now();
    commit.push_back(t1 - t0);
    render.push_back(t2 - t1);
    readBack.push_back(t3 - t2);
    frame.push_back(t3 - t0);
  }
  s.reportTraffic(before, frames);
  s.reportMemory();

  out("frameMedianMs", 1e3 * median(frame));
  out("frameP99Ms", 1e3 * percentile(frame, 0.99));
  out("commitMedianUs", 1e6 * median(commit));
  out("renderMedianUs", 1e6 * median(render));
  out("readBackMedianUs", 1e6 * median(readBack));

  scene.release();
  s.finish();
}

// A volume of 'nx' by 'ny' by 'nz' floats and a view of it: the one large
// array of the upload and update workloads.
struct VolumeScene
{
  VolumeScene(Session &s, const Arguments &a)
      : d(s.device),
        nx(uint64_t(number(a, "nx", 640))),
        ny(uint64_t(number(a, "ny", 640))),
        nz(uint64_t(number(a, "nz", 640))),
        shared(number(a, "shared", 1))
  {
    world = anariNewWorld(d);
    anariCommitParameters(d, world);
    const float position[3] = {0.f, 0.f, -3.5f};
    const float direction[3] = {0.f, 0.f, 1.f};
    const float up[3] = {0.f, 1.f, 0.f};
    view = new View(d, world, a, position, direction, up);
    if (shared) {
      memory.resize(nx * ny * nz);
      fill(memory.data(), 0);
    }
  }

  size_t bytes() const
  {
    return nx * ny * nz * sizeof(float);
  }

  // Write every voxel.
  void fill(float *voxels, int phase) const
  {
    const size_t slice = nx * ny;
    for (uint64_t z = 0; z < nz; z++)
      std::fill_n(
          voxels + z * slice, slice, float((z + phase) % nz) / float(nz));
  }

  // Make the array, as a shared array of the application's memory or as a
  // managed one that is mapped and written; then everything that uses it.
  void create()
  {
    if (shared) {
      data = anariNewArray3D(
          d, memory.data(), nullptr, nullptr, ANARI_FLOAT32, nx, ny, nz);
    } else {
      data = anariNewArray3D(
          d, nullptr, nullptr, nullptr, ANARI_FLOAT32, nx, ny, nz);
      fill(static_cast<float *>(anariMapArray(d, data)), 0);
      anariUnmapArray(d, data);
    }

    field = anariNewSpatialField(d, "structuredRegular");
    const float origin[3] = {-1.f, -1.f, -1.f};
    const float spacing[3] = {2.f / nx, 2.f / ny, 2.f / nz};
    anariSetParameter(d, field, "origin", ANARI_FLOAT32_VEC3, origin);
    anariSetParameter(d, field, "spacing", ANARI_FLOAT32_VEC3, spacing);
    anariSetParameter(d, field, "data", ANARI_ARRAY3D, &data);
    anariCommitParameters(d, field);

    volume = anariNewVolume(d, "transferFunction1D");
    static const float colors[2][3] = {{0.f, 0.f, 1.f}, {1.f, 0.f, 0.f}};
    static const float opacities[2] = {0.f, 1.f};
    static const float range[2] = {0.f, 1.f};
    ANARIArray1D color =
        anariNewArray1D(d, colors, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
    ANARIArray1D opacity =
        anariNewArray1D(d, opacities, nullptr, nullptr, ANARI_FLOAT32, 2);
    anariSetParameter(d, volume, "value", ANARI_SPATIAL_FIELD, &field);
    anariSetParameter(d, volume, "color", ANARI_ARRAY1D, &color);
    anariSetParameter(d, volume, "opacity", ANARI_ARRAY1D, &opacity);
    anariSetParameter(d, volume, "valueRange", ANARI_FLOAT32_BOX1, range);
    anariCommitParameters(d, volume);
    anariRelease(d, color);
    anariRelease(d, opacity);

    ANARIArray1D volumes =
        anariNewArray1D(d, &volume, nullptr, nullptr, ANARI_VOLUME, 1);
    anariSetParameter(d, world, "volume", ANARI_ARRAY1D, &volumes);
    anariCommitParameters(d, world);
    anariRelease(d, volumes);
  }

  // Map the array, write every voxel, unmap it. Returns the time the unmap
  // took, which is where a shared array is copied.
  double rewrite(int phase)
  {
    auto *voxels = static_cast<float *>(anariMapArray(d, data));
    if (!voxels)
      fail("the array did not map");
    fill(voxels, phase);
    const double t = now();
    anariUnmapArray(d, data);
    return now() - t;
  }

  void release()
  {
    view->release();
    delete view;
    anariRelease(d, volume);
    anariRelease(d, field);
    anariRelease(d, data);
    anariRelease(d, world);
  }

  ANARIDevice d;
  uint64_t nx, ny, nz;
  bool shared;
  std::vector<float> memory; // the application's own, for a shared array
  ANARIWorld world{nullptr};
  View *view{nullptr};
  ANARIArray3D data{nullptr};
  ANARISpatialField field{nullptr};
  ANARIVolume volume{nullptr};
};

// Workload 2: a large array, from its creation to the first frame that shows
// it.
void upload(const Arguments &a)
{
  Session s(a);
  VolumeScene scene(s, a);
  // An empty frame first, so that nothing the device puts off until its
  // first render is counted.
  scene.view->render();
  scene.view->readBack();

  const Traffic before = s.traffic();
  const double start = now();
  scene.create();
  scene.view->render();
  scene.view->readBack();
  out("uploadSeconds", now() - start);
  out("arrayBytes", double(scene.bytes()));
  s.reportTraffic(before);
  s.reportMemory();

  scene.release();
  s.finish();
}

// Workload 3: the same kind of array, mapped, rewritten and unmapped before
// every frame. The camera moves as well, because a device may decline to
// render a frame in which only an array's contents changed (helide does).
void update(const Arguments &a)
{
  const int frames = int(number(a, "frames", 100));
  const int warmup = int(number(a, "warmup", 10));

  Session s(a);
  VolumeScene scene(s, a);
  scene.create();
  for (int i = 0; i < warmup; i++) {
    scene.rewrite(i);
    scene.view->moveCamera(i);
    scene.view->render();
    scene.view->readBack();
  }

  std::vector<double> frame, rewrite, unmap;
  const Traffic before = s.traffic();
  for (int i = 0; i < frames; i++) {
    const double t0 = now();
    unmap.push_back(scene.rewrite(warmup + i));
    const double t1 = now();
    scene.view->moveCamera(i);
    scene.view->render();
    scene.view->readBack();
    const double t2 = now();
    rewrite.push_back(t1 - t0);
    frame.push_back(t2 - t0);
  }
  s.reportTraffic(before, frames);
  s.reportMemory();
  out("arrayBytes", double(scene.bytes()));
  out("frameMedianMs", 1e3 * median(frame));
  out("frameP99Ms", 1e3 * percentile(frame, 0.99));
  out("rewriteMedianMs", 1e3 * median(rewrite));
  out("unmapMedianMs", 1e3 * median(unmap));

  scene.release();
  s.finish();
}

// Workload 4: many small calls. Objects created, given two parameters and
// committed, which are all pipelined calls; then calls that return a value,
// each of which is a round trip.
void smallCalls(const Arguments &a)
{
  const int objects = int(number(a, "objects", 100000));
  const int queries = int(number(a, "queries", 100000));

  Session s(a);
  ANARIDevice d = s.device;
  s.sync();

  std::vector<ANARIMaterial> materials(objects);
  const float color[3] = {0.2f, 0.4f, 0.6f};
  const float opacity = 0.5f;
  const Traffic before = s.traffic();
  double t = now();
  for (auto &m : materials) {
    m = anariNewMaterial(d, "matte");
    anariSetParameter(d, m, "color", ANARI_FLOAT32_VEC3, color);
    anariSetParameter(d, m, "opacity", ANARI_FLOAT32, &opacity);
    anariCommitParameters(d, m);
  }
  s.sync(); // the host has executed them all
  out("objectMicroseconds", 1e6 * (now() - t) / objects);
  s.reportTraffic(before, objects);
  s.reportMemory();

  t = now();
  for (auto &m : materials)
    anariRelease(d, m);
  s.sync();
  out("releaseMicroseconds", 1e6 * (now() - t) / objects);

  ANARIFrame frame = anariNewFrame(d);
  s.sync();
  t = now();
  for (int i = 0; i < queries; i++) {
    float duration = 0.f;
    anariGetProperty(d,
        frame,
        "duration",
        ANARI_FLOAT32,
        &duration,
        sizeof(duration),
        ANARI_NO_WAIT);
  }
  out("queryMicroseconds", 1e6 * (now() - t) / queries);
  anariRelease(d, frame);

  s.finish();
}

// Workload 5: a device created, committed and released, over and over.
void createAndRelease(const Arguments &a)
{
  const int devices = int(number(a, "devices", 20));

  Session s(a);
  std::vector<double> times;
  for (int i = 0; i < devices; i++) {
    anariRelease(s.device, s.device);
    const double t = now();
    s.newDevice();
    anariCommitParameters(s.device, s.device);
    anariRelease(s.device, s.device);
    times.push_back(now() - t);
    s.newDevice();
  }
  out("createMedianMs", 1e3 * median(times));
  out("createFirstMs", 1e3 * times.front());
  s.finish();
}

// One render that takes a long time, and the processor time spent while it
// goes on: the application's own and, through the guard device, the host's.
void longRender(const Arguments &a)
{
  Session s(a);
  SphereScene scene(s.device, a);
  View &view = *scene.view;
  view.render();
  view.readBack();

  const double cpu = s.cpu();
  const double hostCpu = s.hostCpu();
  const double start = now();
  view.moveCamera(1);
  view.render();
  out("renderSeconds", now() - start);
  out("cpuAppSeconds", s.cpu() - cpu);
  out("cpuHostSeconds", s.hostCpu() - hostCpu);
  out("cpuSeconds", (s.cpu() - cpu) + (s.hostCpu() - hostCpu));

  scene.release();
  s.finish();
}

// A frame rendered again with nothing changed, which a device may decline to
// render: how long until the application's wait returns.
void declinedRender(const Arguments &a)
{
  const int renders = int(number(a, "renders", 200));

  Session s(a);
  SphereScene scene(s.device, a);
  View &view = *scene.view;
  view.render();
  view.readBack();

  std::vector<double> times;
  for (int i = 0; i < renders; i++) {
    const double t = now();
    view.render();
    times.push_back(now() - t);
  }
  out("declinedMedianMs", 1e3 * median(times));
  out("declinedP99Ms", 1e3 * percentile(times, 0.99));

  scene.release();
  s.finish();
}

// Arrays of one size, which is what the array ceiling goes by. With
// rewrites=0, each is created and released. Otherwise all are created, and
// filled if they are managed arrays, and kept; then each is mapped, changed
// in one element and unmapped, that many times over, and only those are
// timed: what it costs to write an array again, without what the application
// spends on its contents.
void arrays(const Arguments &a)
{
  const int count = int(number(a, "count", 2000));
  const size_t size = size_t(number(a, "size", 16 * 1024));
  const int rewrites = int(number(a, "rewrites", 0));
  const bool managed = number(a, "managed", 0) != 0;

  Session s(a);
  ANARIDevice d = s.device;
  std::vector<uint8_t> memory(size, 7);
  auto make = [&] {
    if (!managed) {
      return anariNewArray1D(
          d, memory.data(), nullptr, nullptr, ANARI_UINT8, size);
    }
    ANARIArray1D array =
        anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_UINT8, size);
    std::memcpy(anariMapArray(d, array), memory.data(), size);
    anariUnmapArray(d, array);
    return array;
  };
  s.sync();

  if (rewrites == 0) {
    const Traffic before = s.traffic();
    const double t = now();
    for (int i = 0; i < count; i++)
      anariRelease(d, make());
    s.sync();
    out("arrayMicroseconds", 1e6 * (now() - t) / count);
    s.reportTraffic(before, count);
    s.finish();
    return;
  }

  // The first of the rewrites is the one that may be given a region, so it
  // is timed apart from the rest. Each timing ends when the host has executed
  // everything.
  std::vector<ANARIArray1D> kept(size_t(count), nullptr);
  for (auto &array : kept)
    array = make();
  s.sync();
  auto rewriteAll = [&](int r) {
    for (ANARIArray1D array : kept) {
      auto *mapped = static_cast<uint8_t *>(anariMapArray(d, array));
      mapped[size_t(r) % size]++;
      anariUnmapArray(d, array);
    }
  };
  const Traffic before = s.traffic();
  double t = now();
  rewriteAll(0);
  s.sync();
  out("firstRewriteMicroseconds", 1e6 * (now() - t) / count);
  if (rewrites > 1) {
    t = now();
    for (int r = 1; r < rewrites; r++)
      rewriteAll(r);
    s.sync();
    out("rewriteMicroseconds",
        1e6 * (now() - t) / (double(count) * (rewrites - 1)));
  }
  s.reportTraffic(before, count);
  for (ANARIArray1D array : kept)
    anariRelease(d, array);
  s.finish();
}

int runWorkload(const Arguments &a)
{
  const std::string workload = text(a, "workload", "");
  if (workload == "loop")
    interactiveLoop(a);
  else if (workload == "upload")
    upload(a);
  else if (workload == "update")
    update(a);
  else if (workload == "calls")
    smallCalls(a);
  else if (workload == "create")
    createAndRelease(a);
  else if (workload == "render")
    longRender(a);
  else if (workload == "declined")
    declinedRender(a);
  else if (workload == "arrays")
    arrays(a);
  else
    fail("no such workload");
  return 0;
}

// The whole bench ////////////////////////////////////////////////////////////

std::string g_self; // this executable
std::string g_log; // what the runs say on stderr

// What several runs of one workload printed: every value of every key.
struct Result
{
  std::map<std::string, std::vector<double>> values;
  bool failed{false};

  double operator()(const char *key) const
  {
    auto it = values.find(key);
    return failed || it == values.end() ? std::nan("") : median(it->second);
  }
  double low(const char *key) const
  {
    auto it = values.find(key);
    return failed || it == values.end()
        ? std::nan("")
        : *std::min_element(it->second.begin(), it->second.end());
  }
  double high(const char *key) const
  {
    auto it = values.find(key);
    return failed || it == values.end()
        ? std::nan("")
        : *std::max_element(it->second.begin(), it->second.end());
  }
};

std::string capture(const std::string &command)
{
  std::string output;
  if (FILE *pipe = popen(command.c_str(), "r")) {
    char buffer[4096];
    size_t n = 0;
    while ((n = fread(buffer, 1, sizeof(buffer), pipe)) > 0)
      output.append(buffer, n);
    pclose(pipe);
  }
  while (!output.empty() && (output.back() == '\n' || output.back() == ' '))
    output.pop_back();
  return output;
}

// One run, in a process of its own, added to 'result'.
void runOnce(Result &result,
    const std::string &device,
    bool guard,
    const std::string &workload,
    const std::string &arguments,
    const std::string &environment)
{
  const std::string command = "env " + environment + " '" + g_self
      + "' run workload=" + workload + " device=" + device + " guard="
      + (guard ? "1" : "0") + " " + arguments + " 2>>'" + g_log + "'";
  std::fprintf(stderr,
      "  %s %s %s %s %s\n",
      workload.c_str(),
      device.c_str(),
      guard ? "guard" : "in-process",
      arguments.c_str(),
      environment.c_str());
  std::istringstream lines(capture(command));
  std::map<std::string, double> values;
  std::string key;
  double value = 0;
  while (lines >> key >> value)
    values[key] = value;
  if (values["ok"] != 1) {
    std::fprintf(stderr, "  FAILED, see %s\n", g_log.c_str());
    result.failed = true;
    return;
  }
  for (auto &v : values)
    result.values[v.first].push_back(v.second);
}

// The same workload both ways, turn about, so that whatever else the machine
// is doing falls on both alike.
struct Pair
{
  Result direct;
  Result guard;
};

Pair compare(const std::string &device,
    const std::string &workload,
    const std::string &arguments,
    int runs)
{
  Pair p;
  for (int i = 0; i < runs; i++) {
    runOnce(p.direct, device, false, workload, arguments, "");
    runOnce(p.guard, device, true, workload, arguments, "");
  }
  return p;
}

Result throughGuard(const std::string &device,
    const std::string &workload,
    const std::string &arguments,
    const std::string &environment,
    int runs)
{
  Result r;
  for (int i = 0; i < runs; i++)
    runOnce(r, device, true, workload, arguments, environment);
  return r;
}

// Formatting /////////////////////////////////////////////////////////////////

std::string fixed(double value, int decimals)
{
  if (std::isnan(value))
    return "failed";
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
  return buffer;
}

// Three significant figures or so, whatever the size.
std::string figure(double value)
{
  if (std::isnan(value))
    return "failed";
  const double size = std::fabs(value);
  if (size == 0)
    return "0";
  if (size >= 0.1)
    return fixed(value, size >= 100 ? 0 : size >= 10 ? 1 : size >= 1 ? 2 : 3);
  // Small: two significant figures
  return fixed(value, 1 - int(std::floor(std::log10(size))));
}

std::string megabytes(double bytes)
{
  return std::isnan(bytes) ? "failed" : fixed(bytes / (1024.0 * 1024.0), 0);
}

std::string whole(double value)
{
  if (std::isnan(value))
    return "failed";
  // Thousands separated, for byte counts
  std::string digits = fixed(value, 0);
  for (int i = int(digits.size()) - 3; i > 0; i -= 3)
    digits.insert(size_t(i), ",");
  return digits;
}

std::string overhead(double direct, double guard)
{
  if (std::isnan(direct) || std::isnan(guard) || direct <= 0)
    return "failed";
  // Past double, a percentage says less than the ratio does.
  if (guard >= 2 * direct)
    return whole(guard / direct) + "×";
  char buffer[64];
  std::snprintf(
      buffer, sizeof(buffer), "%+.1f%%", 100.0 * (guard / direct - 1));
  return buffer;
}

// A difference, with its sign.
std::string added(double direct, double guard)
{
  if (std::isnan(direct) || std::isnan(guard))
    return "failed";
  const std::string difference = figure(guard - direct);
  return guard > direct ? "+" + difference : difference;
}

std::string times(double direct, double guard)
{
  if (std::isnan(direct) || std::isnan(guard) || direct <= 0)
    return "failed";
  return (guard >= 100 * direct ? whole(guard / direct)
                                : figure(guard / direct))
      + "×";
}

std::string row(const std::vector<std::string> &cells)
{
  std::string line = "|";
  for (auto &c : cells)
    line += " " + c + " |";
  return line + "\n";
}

std::string header(const std::vector<std::string> &cells)
{
  std::string line = row(cells) + "|";
  for (size_t i = 0; i < cells.size(); i++)
    line += i == 0 ? " --- |" : " ---: |";
  return line + "\n";
}

std::string deviceName(const std::string &device)
{
  return device;
}

std::vector<std::string> split(const std::string &list)
{
  std::vector<std::string> items;
  std::istringstream in(list);
  std::string item;
  while (std::getline(in, item, ','))
    items.push_back(item);
  return items;
}

bool has(const std::vector<std::string> &items, const std::string &item)
{
  return std::find(items.begin(), items.end(), item) != items.end();
}

std::string machine()
{
  std::string cpu = "unknown processor";
  {
    std::ifstream info("/proc/cpuinfo");
    std::string line;
    while (std::getline(info, line)) {
      if (line.rfind("model name", 0) == 0) {
        cpu = line.substr(line.find(':') + 2);
        break;
      }
    }
  }
  double memory = 0;
  {
    std::ifstream info("/proc/meminfo");
    std::string line;
    long kb = 0;
    while (std::getline(info, line)) {
      if (std::sscanf(line.c_str(), "MemTotal: %ld kB", &kb) == 1)
        memory = double(kb) / (1024.0 * 1024.0);
    }
  }
  utsname name;
  uname(&name);
  std::string m = cpu + ", " + std::to_string(sysconf(_SC_NPROCESSORS_ONLN))
      + " logical processors, " + fixed(memory, 0) + " GB of memory, "
      + name.sysname + " " + name.release;
  return m;
}

int bench(const Arguments &a)
{
  const bool quick = number(a, "quick", 0);
  const int runs = int(number(a, "runs", quick ? 1 : 5));
  const int fewer = std::max(1, std::min(runs, 3));
  const std::string outPath = text(a, "out", "overhead.md");
  const auto devices = split(text(a, "devices", "helide,sink"));
  const auto only =
      split(text(a, "only", "loop,upload,update,calls,create,probe,inline"));
  g_log = outPath + ".log";
  std::remove(g_log.c_str());

  // The guard device is run from where this program is.
  const std::string here = g_self.substr(0, g_self.rfind('/'));
  const char *path = std::getenv("LD_LIBRARY_PATH");
  setenv("LD_LIBRARY_PATH",
      (here + (path ? std::string(":") + path : std::string())).c_str(),
      1);
  setenv("ANARI_GUARD_HOST", (here + "/anariGuardHost").c_str(), 1);

  const std::string source = GUARD_BENCH_SOURCE_DIR;
  std::string commit =
      capture("git -C '" + source + "' rev-parse --short HEAD 2>/dev/null");
  if (commit.empty())
    commit = "unknown";
  else if (!capture(
               "git -C '" + source + "' status --porcelain -- . 2>/dev/null")
                .empty())
    commit += ", with changes not committed";

  std::string md;
  md += "# Guard device overhead\n\n";
  md +=
      "What the guard device costs against the same device loaded "
      "in-process. Written by `guardBench`; do not edit by hand.\n\n";
  md += "- Machine: " + machine() + "\n";
  md += std::string("- Build type: ") + GUARD_BENCH_BUILD_TYPE
      + " (the guard device and this program; the SDK and the devices are as "
        "installed, ANARI-SDK " GUARD_BENCH_SDK_VERSION ")\n";
  md += "- Commit: " + commit + "\n";
  md += "- Date: " + capture("date +%Y-%m-%d") + "\n";
  if (quick)
    md +=
        "- **A quick run**: fewer frames, smaller arrays, one run of each. "
        "Not the figures.\n";
  md += "\nEvery figure is the median of " + std::to_string(runs)
      + " run" + (runs == 1 ? "" : "s") + " (" + std::to_string(fewer)
      + " for the large-array workloads and the sweeps), each in a process of "
        "its own, in-process and guarded runs taken turn about. \"Guard\" is "
        "the device named in the row behind the guard device. Sink renders "
        "nothing, so its rows are the guard device's own cost and its ratios "
        "are not judged.\n\n";

  auto save = [&] {
    std::ofstream file(outPath);
    file << md;
  };

  const std::string loopFrames =
      quick ? "frames=100 warmup=10" : "frames=1000 warmup=100";

  // Interactive loop /////////////////////////////////////////////////////////

  if (has(only, "loop")) {
    struct Variant
    {
      const char *name;
      const char *arguments;
      double megabytesMapped; // each frame, in units of 10^6 bytes
    };
    const Variant variants[] = {
        {"1920×1080, `UFIXED8_RGBA_SRGB`",
            "width=1920 height=1080",
            1920 * 1080 * 4 / 1e6},
        {"3840×2160, `UFIXED8_RGBA_SRGB`",
            "width=3840 height=2160",
            3840 * 2160 * 4 / 1e6},
        {"1920×1080, `FLOAT32_VEC4` and depth",
            "width=1920 height=1080 float=1",
            1920 * 1080 * (16 + 4) / 1e6},
    };
    // The pass mark: what the guard device may add to the judged frame.
    const double hertz = 60, share = 0.05;
    const double allowedMs = share * 1e3 / hertz;
    const std::string allowed = fixed(allowedMs, 2) + " ms";

    std::string headline, perMegabyte, frameTable, costTable, phaseTable;
    frameTable = header({"Device",
        "Frame",
        "In-process median (ms)",
        "Guard median (ms)",
        "Added (ms)",
        "Overhead",
        "In-process 99th (ms)",
        "Guard 99th (ms)"});
    costTable = header({"Device",
        "Frame",
        "First frame, in-process (s)",
        "First frame, guard (s)",
        "Memory, in-process (MB)",
        "Memory, guard (MB)",
        "Sent per frame (bytes)",
        "Received per frame (bytes)",
        "Writes per frame",
        "Regions"});
    phaseTable = header({"Device",
        "Camera change, in-process",
        "guard",
        "Render and wait, in-process",
        "guard",
        "Map and unmap, in-process",
        "guard"});
    for (auto &device : devices) {
      // What mapping and unmapping costs more than in-process, for each
      // megabyte mapped, in each of this device's frames.
      std::vector<double> mapCost;
      for (auto &v : variants) {
        const Pair p =
            compare(device, "loop", loopFrames + " " + v.arguments, runs);
        const double direct = p.direct("frameMedianMs");
        const double guard = p.guard("frameMedianMs");
        mapCost.push_back(
            (p.guard("readBackMedianUs") - p.direct("readBackMedianUs"))
            / v.megabytesMapped);
        if (&v == &variants[std::size(variants) - 1]) {
          const bool measured = std::none_of(mapCost.begin(),
              mapCost.end(),
              [](double c) { return std::isnan(c); });
          perMegabyte += std::string(perMegabyte.empty() ? "" : ", ")
              + (measured ? figure(*std::min_element(
                                mapCost.begin(), mapCost.end()))
                          + " to "
                          + figure(
                              *std::max_element(mapCost.begin(), mapCost.end()))
                          + " µs"
                          : std::string("not measured"))
              + " on " + deviceName(device);
        }
        frameTable += row({deviceName(device),
            v.name,
            figure(direct),
            figure(guard),
            added(direct, guard),
            overhead(direct, guard),
            figure(p.direct("frameP99Ms")),
            figure(p.guard("frameP99Ms"))});
        costTable += row({deviceName(device),
            v.name,
            figure(p.direct("firstFrameSeconds")),
            figure(p.guard("firstFrameSeconds")),
            megabytes(p.direct("pss")),
            megabytes(p.guard("pss")),
            whole(p.guard("bytesSent")),
            whole(p.guard("bytesReceived")),
            figure(p.guard("flushes")),
            whole(p.guard("regionsCreated"))});
        if (&v != &variants[0])
          continue;
        phaseTable += row({deviceName(device),
            figure(p.direct("commitMedianUs")),
            figure(p.guard("commitMedianUs")),
            figure(p.direct("renderMedianUs")),
            figure(p.guard("renderMedianUs")),
            figure(p.direct("readBackMedianUs")),
            figure(p.guard("readBackMedianUs"))});
        if (device == "sink")
          continue;
        const bool within = guard - direct <= allowedMs;
        headline += "- **" + deviceName(device) + "**: " + figure(direct)
            + " ms in-process, " + figure(guard)
            + " ms through the guard device, " + added(direct, guard) + " ms ("
            + overhead(direct, guard) + "): **"
            + (std::isnan(direct) || std::isnan(guard) ? "not measured"
                    : within                           ? "within " + allowed
                             : "not within " + allowed)
            + "**. Across the runs the in-process median was "
            + figure(p.direct.low("frameMedianMs")) + " to "
            + figure(p.direct.high("frameMedianMs")) + " ms and the guarded "
            + figure(p.guard.low("frameMedianMs")) + " to "
            + figure(p.guard.high("frameMedianMs")) + " ms.\n";
      }
    }
    md += "## Headline\n\n";
    md += "The interactive loop at 1920×1080 with `UFIXED8_RGBA_SRGB` colour, "
          "median frame time. Through the guard device a frame may take at "
          "most "
        + allowed + " longer than in-process, on each device that renders: "
        + allowed + " is " + fixed(100 * share, 0) + "% of a frame at "
        + fixed(hertz, 0)
        + " Hz. The relative overhead, in brackets, is recorded and not "
          "judged.\n\n";
    md += headline + "\n";
    md += "## Interactive loop\n\n";
    md +=
        "The SDK's `random_spheres` test scene with the device's default "
        "renderer. Each frame changes and commits the camera position, "
        "renders, waits, and maps and unmaps what the frame holds; the "
        "pixels are not read. "
        + std::string(quick ? "100 frames after 10" : "1,000 frames after 100")
        + " to warm up.\n\n";
    md += frameTable + "\n";
    md += "Only the first row of each device that renders is judged, in the "
          "headline; the other frame rows are recorded and not judged. The "
          "guard device's cost grows with the bytes the application maps, "
          "because the host copies each mapped channel once: for each megabyte "
          "mapped in a frame, mapping and unmapping took longer than "
          "in-process by "
        + perMegabyte
        + ", the lowest and the highest of the device's three frames. A "
          "1920×1080 `UFIXED8_RGBA_SRGB` channel is "
        + figure(variants[0].megabytesMapped) + " MB.\n\n";
    md +=
        "Where a frame's time goes in the first of those, in microseconds "
        "(medians of each part, so they need not add up to the frame):\n\n";
    md += phaseTable + "\n";
    md +=
        "Time to the first frame is from before the library is loaded until "
        "the first frame has been mapped. Memory is proportional set size, "
        "of the application in-process and of application plus host through "
        "the guard device, sampled after the last frame with everything "
        "still alive and its regions mapped. Traffic is the adapter's own "
        "count over the timed frames, on both channels; regions are all "
        "those made since the device was created.\n\n";
    md += costTable + "\n";
    save();
  }

  // Large-array upload ///////////////////////////////////////////////////////

  const std::string large =
      quick ? "nx=256 ny=256 nz=256" : "nx=640 ny=640 nz=640";
  const std::string medium =
      quick ? "nx=128 ny=128 nz=128" : "nx=320 ny=320 nz=256";

  if (has(only, "upload")) {
    md += "## Large-array upload\n\n";
    md += std::string("A volume of ")
        + (quick ? "256×256×256" : "640×640×640") + " `FLOAT32` ("
        + (quick ? "64 MB" : "1.0 GB")
        + ") as a structured regular field, timed from the creation of its "
          "array to the first 1920×1080 frame that shows it. A shared array "
          "is the application's own memory, already filled; a managed array "
          "is created empty, mapped and filled, and filling it is part of "
          "the time. Memory as above.\n\n";
    md += header({"Device",
        "Array",
        "In-process (s)",
        "Guard (s)",
        "Ratio",
        "Memory, in-process (MB)",
        "Memory, guard (MB)",
        "Sent (bytes)",
        "Regions"});
    for (auto &device : devices) {
      for (int shared = 1; shared >= 0; shared--) {
        const Pair p = compare(device,
            "upload",
            large + " shared=" + std::to_string(shared),
            fewer);
        md += row({deviceName(device),
            shared ? "shared" : "managed",
            figure(p.direct("uploadSeconds")),
            figure(p.guard("uploadSeconds")),
            times(p.direct("uploadSeconds"), p.guard("uploadSeconds")),
            megabytes(p.direct("pss")),
            megabytes(p.guard("pss")),
            whole(p.guard("bytesSent")),
            whole(p.guard("regionsCreated"))});
      }
    }
    md += "\n";
    save();
  }

  // Per-frame array update ///////////////////////////////////////////////////

  if (has(only, "update")) {
    md += "## Per-frame array update\n\n";
    md += std::string("The same kind of volume at ")
        + (quick ? "128×128×128 (8 MB)" : "320×320×256 (100 MB)")
        + ", mapped, rewritten in full and unmapped before every 1920×1080 "
          "frame, with the camera moved as in the interactive loop. "
        + (quick ? "10 frames after 2" : "100 frames after 10")
        + " to warm up. The unmap is timed on its own because that is where "
          "a shared array is copied.\n\n";
    md += header({"Device",
        "Array",
        "In-process median (ms)",
        "Guard median (ms)",
        "Overhead",
        "In-process 99th (ms)",
        "Guard 99th (ms)",
        "Unmap, in-process (ms)",
        "Unmap, guard (ms)"});
    for (auto &device : devices) {
      for (int shared = 1; shared >= 0; shared--) {
        const Pair p = compare(device,
            "update",
            medium + " shared=" + std::to_string(shared)
                + (quick ? " frames=10 warmup=2" : " frames=100 warmup=10"),
            fewer);
        md += row({deviceName(device),
            shared ? "shared" : "managed",
            figure(p.direct("frameMedianMs")),
            figure(p.guard("frameMedianMs")),
            overhead(p.direct("frameMedianMs"), p.guard("frameMedianMs")),
            figure(p.direct("frameP99Ms")),
            figure(p.guard("frameP99Ms")),
            figure(p.direct("unmapMedianMs")),
            figure(p.guard("unmapMedianMs"))});
      }
    }
    md += "\n";
    save();
  }

  // Many small calls /////////////////////////////////////////////////////////

  if (has(only, "calls")) {
    const std::string count = quick ? "5000" : "100000";
    md += "## Many small calls\n\n";
    md += std::string(quick ? "5,000" : "100,000")
        + " `matte` materials, each created, given two parameters and "
          "committed (four pipelined calls), timed until the device has "
          "executed them all; then released; then as many "
          "`anariGetProperty` calls on a frame, each of which waits for the "
          "host's answer. Microseconds per object and per call.\n\n";
    md += header({"Device",
        "Object, in-process (µs)",
        "Object, guard (µs)",
        "Release, in-process (µs)",
        "Release, guard (µs)",
        "Value-returning call, in-process (µs)",
        "Value-returning call, guard (µs)",
        "Sent per object (bytes)",
        "Writes per object"});
    for (auto &device : devices) {
      const Pair p = compare(
          device, "calls", "objects=" + count + " queries=" + count, runs);
      md += row({deviceName(device),
          figure(p.direct("objectMicroseconds")),
          figure(p.guard("objectMicroseconds")),
          figure(p.direct("releaseMicroseconds")),
          figure(p.guard("releaseMicroseconds")),
          figure(p.direct("queryMicroseconds")),
          figure(p.guard("queryMicroseconds")),
          figure(p.guard("bytesSent")),
          figure(p.guard("flushes"))});
    }
    md += "\n";
    save();
  }

  // Device creation and release //////////////////////////////////////////////

  if (has(only, "create")) {
    md += "## Device creation and release\n\n";
    md +=
        "`anariNewDevice`, a commit of the device and its release, 20 times "
        "over with the library already loaded. Through the guard device "
        "each is a host process started, the wrapped library loaded in it, "
        "and the host ended.\n\n";
    md += header({"Device",
        "In-process median (ms)",
        "Guard median (ms)",
        "In-process first (ms)",
        "Guard first (ms)"});
    for (auto &device : devices) {
      const Pair p = compare(device, "create", "devices=20", runs);
      md += row({deviceName(device),
          figure(p.direct("createMedianMs")),
          figure(p.guard("createMedianMs")),
          figure(p.direct("createFirstMs")),
          figure(p.guard("createFirstMs"))});
    }
    md += "\n";
    save();
  }

  // Sweeps ///////////////////////////////////////////////////////////////////

  if (has(only, "probe")) {
    md += "## Sweep: probe period\n\n";
    md +=
        "`ANARI_GUARD_PROBE_PERIOD_US`, whose default is 1000. Through the "
        "guard device only: the interactive loop's median frame at "
        "1920×1080 on sink and on helide, the wait for a frame rendered "
        "again unchanged on helide (which helide declines to render). "
        "The default has not been changed.\n\n";
    md += header({"Probe period (µs)",
        "sink frame (ms)",
        "helide frame (ms)",
        "helide declined render (ms)",
        "helide declined render, 99th (ms)"});
    for (const char *period : {"100", "250", "1000", "4000"}) {
      const std::string environment =
          std::string("ANARI_GUARD_PROBE_PERIOD_US=") + period;
      std::vector<std::string> cells = {period};
      const std::string loop = loopFrames + " width=1920 height=1080";
      cells.push_back(has(devices, "sink")
              ? figure(throughGuard("sink", "loop", loop, environment, fewer)(
                    "frameMedianMs"))
              : "");
      if (has(devices, "helide")) {
        cells.push_back(figure(throughGuard(
            "helide", "loop", loop, environment, fewer)("frameMedianMs")));
        const Result declined = throughGuard("helide",
            "declined",
            "width=1920 height=1080 renders=200",
            environment,
            fewer);
        cells.push_back(figure(declined("declinedMedianMs")));
        cells.push_back(figure(declined("declinedP99Ms")));
      } else
        cells.insert(cells.end(), {"", "", ""});
      md += row(cells);
    }
    md += "\n";
    save();
  }

  if (has(only, "inline")) {
    md += "## Sweep: array ceiling\n\n";
    md +=
        "`ANARI_GUARD_INLINE_THRESHOLD`, whose default is 1048576 (1 MB), "
        "sets the array ceiling. An array of at least that size crosses in "
        "a region from the start; a smaller one crosses in its message the "
        "first time and is given a region when it is written again. Frame "
        "channels do not go by it. Through the guard device only, in "
        "microseconds: to create and release one shared array of each size, "
        "timed until the host has executed them all; and, for an array that "
        "is kept, the first time it is written again and each time after "
        "that (mapped, one element changed and unmapped; four times after "
        "the first), likewise timed until the host has executed them "
        "all.\n\n";
    const char *thresholds[] = {"65536", "1048576", "16777216"};
    md += header({"", "64 KB", "1 MB (default)", "16 MB"});
    // Size, bytes, and how many arrays a full run makes
    const char *sizes[][3] = {{"32 KB", "32768", "2000"},
        {"128 KB", "131072", "1000"},
        {"512 KB", "524288", "400"},
        {"2 MB", "2097152", "100"},
        {"8 MB", "8388608", "32"}};
    for (auto &device : devices) {
      for (auto &size : sizes) {
        const std::string count =
            std::string(" count=") + (quick ? "20" : size[2]);
        std::vector<std::string> made = {
            device + ": shared array of " + size[0] + ", made once"};
        std::vector<std::string> first = {
            device + ": the same, first written again"};
        std::vector<std::string> later = {
            device + ": the same, each time after"};
        std::vector<std::string> managedFirst = {
            device + ": managed array of " + size[0] + ", first written again"};
        std::vector<std::string> managedLater = {
            device + ": the same, each time after"};
        for (const char *threshold : thresholds) {
          const std::string environment =
              std::string("ANARI_GUARD_INLINE_THRESHOLD=") + threshold;
          const std::string arguments = std::string("size=") + size[1] + count;
          made.push_back(figure(
              throughGuard(device, "arrays", arguments, environment, fewer)(
                  "arrayMicroseconds")));
          const Result shared = throughGuard(
              device, "arrays", arguments + " rewrites=5", environment, fewer);
          first.push_back(figure(shared("firstRewriteMicroseconds")));
          later.push_back(figure(shared("rewriteMicroseconds")));
          const Result managed = throughGuard(device,
              "arrays",
              arguments + " rewrites=5 managed=1",
              environment,
              fewer);
          managedFirst.push_back(figure(managed("firstRewriteMicroseconds")));
          managedLater.push_back(figure(managed("rewriteMicroseconds")));
        }
        md += row(made) + row(first) + row(later) + row(managedFirst)
            + row(managedLater);
      }
    }
    md += "\n";
    save();
  }

  save();
  std::fprintf(stderr, "wrote %s\n", outPath.c_str());
  return 0;
}

} // namespace

int main(int argc, char *argv[])
{
  if (argc > 1 && std::strcmp(argv[1], "run") == 0)
    return runWorkload(parse(argc, argv, 2));

  char self[4096];
  const ssize_t length = readlink("/proc/self/exe", self, sizeof(self) - 1);
  if (length <= 0) {
    std::fprintf(stderr, "guardBench: cannot find its own executable\n");
    return 2;
  }
  g_self.assign(self, size_t(length));
  return bench(parse(argc, argv, 1));
}
