// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// MPI lifetime test for the ptc device. When the app has not
// initialized MPI, ptc does it privately and must keep it initialized until
// the last ptc device is released; when the app has, ptc must never finalize
// it.
//
// Usage: ptcLifetime --scenario two-devices|single|query-release
//                    [--app-mpi] [--expect-library <path>]
//
//   two-devices    create two devices and render with both; release the
//                  first (MPI must stay initialized), render with the second
//                  again, then release it
//   single         create one device, render, release it
//   query-release  create one device, query its 'extension' property (which
//                  initializes the device, and so maybe MPI), release it
//   --app-mpi      the app calls MPI_Init before and MPI_Finalize after
//                  everything; otherwise it never calls either, and once the
//                  last device is gone MPI must be finalized
//
// Rendering: rank r draws one sphere at a distinct x position with a distinct
// color; the composited image's rank 0 checks every rank's sphere.
//
// Without --app-mpi the ranks cannot agree on the result after ptc finalized
// MPI, so each rank exits non-zero on its own failures; mpiexec then fails.

// anari
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// mpi
#include <mpi.h>
// std
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

using namespace anari::math;

namespace {

const float3 COLORS[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}};
const int NUM_COLORS = int(sizeof(COLORS) / sizeof(COLORS[0]));
const uint2 IMAGE_SIZE(64, 32);
const float CAMERA_HEIGHT = 1.5f;
const float ASPECT = float(IMAGE_SIZE.x) / IMAGE_SIZE.y;

int g_errors = 0;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity sev,
    ANARIStatusCode,
    const char *msg)
{
  if (sev <= ANARI_SEVERITY_WARNING)
    fprintf(stderr, "[anari] %s\n", msg);
  if (sev <= ANARI_SEVERITY_ERROR)
    g_errors++;
}

std::string canonicalPath(const std::string &path)
{
  char buf[PATH_MAX];
  return realpath(path.c_str(), buf) ? std::string(buf) : path;
}

// Path of the libanari_library_ptc mapped into this process, or "" if none
std::string loadedPtcLibrary()
{
  std::ifstream maps("/proc/self/maps");
  std::string line;
  while (std::getline(maps, line)) {
    auto pos = line.find('/');
    if (pos != std::string::npos
        && line.find("libanari_library_ptc", pos) != std::string::npos)
      return canonicalPath(line.substr(pos));
  }
  return "";
}

bool mpiInitialized()
{
  int flag = 0;
  MPI_Initialized(&flag);
  return flag;
}

bool mpiFinalized()
{
  int flag = 0;
  MPI_Finalized(&flag);
  return flag;
}

struct Checker
{
  int failures{0};

  void expect(bool ok, const std::string &what);
};

void Checker::expect(bool ok, const std::string &what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", what.c_str());
  failures += !ok;
}

float sphereX(int r, int size)
{
  return size == 1 ? 0.f : -1.f + 2.f * r / (size - 1);
}

int pixelX(float x)
{
  return int((x / (CAMERA_HEIGHT * ASPECT) + 0.5f) * IMAGE_SIZE.x);
}

// One device with a frame that draws this rank's sphere. release() releases
// the frame before the device: ptc frames must not outlive their device.
struct Renderer
{
  anari::Device device{nullptr};
  anari::Frame frame{nullptr};

  // Create the device and initialize it (and so maybe MPI) by querying a
  // device property.
  void create(Checker &c, anari::Library lib);

  void build(int rank, int size);

  // Render; rank 0 checks every rank's sphere.
  void render(Checker &c, const std::string &what, int rank, int size);

  void release();
};

void Renderer::create(Checker &c, anari::Library lib)
{
  device = anari::newDevice(lib, "default");
  const char *const *exts = nullptr;
  c.expect(anariGetProperty(device,
               device,
               "extension",
               ANARI_STRING_LIST,
               &exts,
               sizeof(exts),
               ANARI_WAIT)
          && exts,
      "device property 'extension' reported");
  c.expect(mpiInitialized() && !mpiFinalized(),
      "MPI initialized once the device is");
}

void Renderer::build(int rank, int size)
{
  auto d = device;
  const float3 pos(sphereX(rank, size), 0.f, 0.f);
  auto geom = anari::newObject<anari::Geometry>(d, "sphere");
  anari::setParameterArray1D(d, geom, "vertex.position", &pos, 1);
  anari::setParameter(d, geom, "radius", 0.3f);
  anari::commitParameters(d, geom);
  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", COLORS[rank % NUM_COLORS]);
  anari::commitParameters(d, mat);
  auto surf = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surf, "geometry", geom);
  anari::setAndReleaseParameter(d, surf, "material", mat);
  anari::commitParameters(d, surf);
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surf, 1);
  anari::release(d, surf);
  auto light = anari::newObject<anari::Light>(d, "directional");
  anari::setParameter(d, light, "direction", float3(0, 0, -1));
  anari::commitParameters(d, light);
  anari::setParameterArray1D(d, world, "light", &light, 1);
  anari::release(d, light);
  anari::commitParameters(d, world);

  auto cam = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, cam, "position", float3(0, 0, 5));
  anari::setParameter(d, cam, "direction", float3(0, 0, -1));
  anari::setParameter(d, cam, "up", float3(0, 1, 0));
  anari::setParameter(d, cam, "height", CAMERA_HEIGHT);
  anari::setParameter(d, cam, "aspect", ASPECT);
  anari::commitParameters(d, cam);

  auto ren = anari::newObject<anari::Renderer>(d, "default");
  anari::setParameter(d, ren, "ambientRadiance", 1.f);
  anari::commitParameters(d, ren);

  frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_UFIXED8_VEC4);
  anari::setAndReleaseParameter(d, frame, "world", world);
  anari::setAndReleaseParameter(d, frame, "camera", cam);
  anari::setAndReleaseParameter(d, frame, "renderer", ren);
  anari::commitParameters(d, frame);
}

void Renderer::render(Checker &c, const std::string &what, int rank, int size)
{
  const int errorsBefore = g_errors;
  anari::render(device, frame);
  anari::wait(device, frame);
  c.expect(g_errors == errorsBefore, what + ": renders without errors");
  if (rank != 0)
    return;
  auto fb = anari::map<uint32_t>(device, frame, "channel.color");
  if (!fb.data || fb.width != IMAGE_SIZE.x || fb.height != IMAGE_SIZE.y) {
    c.expect(false, what + ": color map");
  } else {
    for (int r = 0; r < size; r++) {
      const int px = pixelX(sphereX(r, size));
      const uint32_t v = fb.data[(IMAGE_SIZE.y / 2) * IMAGE_SIZE.x + px];
      const float3 want = COLORS[r % NUM_COLORS];
      bool ok = ((v >> 24) & 0xff) > 230;
      for (int i = 0; i < 3; i++)
        ok &= (want[i] > 0.5f) == (((v >> (8 * i)) & 0xff) > 25);
      c.expect(ok, what + ": rank " + std::to_string(r) + "'s sphere");
    }
  }
  anari::unmap(device, frame, "channel.color");
}

void Renderer::release()
{
  if (frame)
    anari::release(device, frame);
  anari::release(device, device);
  frame = nullptr;
  device = nullptr;
}

} // namespace

int main(int argc, char **argv)
{
  std::string scenario;
  std::string expectLibrary;
  bool appMpi = false;
  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];
    if (arg == "--scenario" && i + 1 < argc)
      scenario = argv[++i];
    else if (arg == "--expect-library" && i + 1 < argc)
      expectLibrary = argv[++i];
    else if (arg == "--app-mpi")
      appMpi = true;
    else {
      fprintf(stderr, "unknown argument '%s'\n", arg.c_str());
      return 2;
    }
  }
  if (scenario != "two-devices" && scenario != "single"
      && scenario != "query-release") {
    fprintf(stderr, "missing or unknown --scenario\n");
    return 2;
  }

  if (appMpi)
    MPI_Init(&argc, &argv);

  Checker c;
  auto lib = anari::loadLibrary("ptc", statusFunc);
  if (!lib) {
    printf("FAIL: could not load the ptc library\n");
    return 1;
  }
  if (!expectLibrary.empty()) {
    c.expect(loadedPtcLibrary() == canonicalPath(expectLibrary),
        "loaded ptc from '" + loadedPtcLibrary() + "', expected '"
            + expectLibrary + "'");
  }

  // Without --app-mpi, MPI only becomes usable once a device initialized it.
  Renderer first;
  first.create(c, lib);
  int rank = 0, size = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  if (scenario == "single") {
    first.build(rank, size);
    first.render(c, "single device", rank, size);
  } else if (scenario == "two-devices") {
    Renderer second;
    second.create(c, lib);
    first.build(rank, size);
    second.build(rank, size);
    first.render(c, "first device", rank, size);
    second.render(c, "second device", rank, size);
    // device release is collective: every rank releases the first one here
    first.release();
    c.expect(mpiInitialized() && !mpiFinalized(),
        "MPI still initialized after releasing the first device");
    second.render(c, "second device, after releasing the first", rank, size);
    second.release();
  }
  if (first.device)
    first.release();
  anari::unloadLibrary(lib);

  const bool finalized = mpiFinalized();
  if (appMpi) {
    c.expect(!finalized, "app-initialized MPI not finalized by ptc");
    int total = 0;
    MPI_Allreduce(&c.failures, &total, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    MPI_Finalize();
    if (rank == 0)
      printf(total ? "LIFETIME FAILED (%d)\n" : "LIFETIME OK\n", total);
    return total ? 1 : 0;
  }
  // MPI is gone, so each rank reports only its own result
  c.expect(finalized, "MPI finalized after releasing the last device");
  printf(
      c.failures ? "rank %d: LIFETIME FAILED (%d)\n" : "rank %d: LIFETIME OK\n",
      rank,
      c.failures);
  return c.failures ? 1 : 0;
}
