// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Shared harness of ptcSmoke: see SmokeHarness.h.

// ptc
#include "SmokeHarness.h"
// std
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace ptcsmoke {

// surface id (objectId) of the sphere of the rank with this world rank
uint32_t surfaceId(int worldRank)
{
  return 100u + uint32_t(worldRank);
}

// Status messages at WARNING or worse that mention channel.depth/color
int g_depthWarnings = 0;
int g_colorWarnings = 0;
// Status messages at ERROR or worse that mention channel.color
int g_colorErrors = 0;
// Status messages at ERROR or worse
int g_errors = 0;
// Status messages at WARNING or worse
int g_warnings = 0;
// Status messages at ERROR or worse with ANARI_STATUS_INVALID_ARGUMENT that
// mention channel.color
int g_colorInvalidArguments = 0;
// The last status message at WARNING or worse
std::string g_lastWarning;
// Every status message at WARNING or worse, in order
std::vector<std::string> g_warningLog;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity sev,
    ANARIStatusCode code,
    const char *msg)
{
  const bool color =
      std::string(msg).find("channel.color") != std::string::npos;
  if (sev <= ANARI_SEVERITY_WARNING) {
    fprintf(stderr, "[anari] %s\n", msg);
    g_depthWarnings +=
        std::string(msg).find("channel.depth") != std::string::npos;
    g_colorWarnings += color;
    g_lastWarning = msg;
    g_warningLog.push_back(msg);
    g_warnings++;
  }
  if (sev <= ANARI_SEVERITY_ERROR) {
    g_colorErrors += color;
    g_colorInvalidArguments += color && code == ANARI_STATUS_INVALID_ARGUMENT;
    g_errors++;
  }
}

void Checker::expect(bool ok, const std::string &what)
{
  if (!ok) {
    printf("FAIL: rank %d: %s\n", rank, what.c_str());
    failures++;
  }
}

namespace {

bool parseColorType(const std::string &type, ANARIDataType &out)
{
  if (type == "ufixed8")
    out = ANARI_UFIXED8_RGBA_SRGB;
  else if (type == "ufixed8-linear")
    out = ANARI_UFIXED8_VEC4;
  else if (type == "float32")
    out = ANARI_FLOAT32_VEC4;
  else if (type == "float32-vec3")
    out = ANARI_FLOAT32_VEC3;
  else if (type == "none")
    out = ANARI_UNKNOWN;
  else {
    fprintf(stderr, "unknown color type '%s'\n", type.c_str());
    return false;
  }
  return true;
}

bool parseBackground(const std::string &bg, Background &out)
{
  if (bg == "none")
    out = Background::None;
  else if (bg == "blend")
    out = Background::Blend;
  else if (bg == "opaque")
    out = Background::Opaque;
  else {
    fprintf(stderr, "unknown background '%s'\n", bg.c_str());
    return false;
  }
  return true;
}

bool parseComm(const std::string &comm, CommSplit &split)
{
  if (comm == "world")
    split = CommSplit::World;
  else if (comm == "split")
    split = CommSplit::Halves;
  else if (comm == "interleaved")
    split = CommSplit::Interleaved;
  else {
    fprintf(stderr, "unknown communicator '%s'\n", comm.c_str());
    return false;
  }
  return true;
}

} // namespace

// Color types ptc composites (or ANARI_UNKNOWN: no color)
bool compositable(ANARIDataType type)
{
  return type == ANARI_UNKNOWN || type == ANARI_UFIXED8_VEC4
      || type == ANARI_UFIXED8_RGBA_SRGB || type == ANARI_FLOAT32_VEC4;
}

bool parseArgs(int argc, char **argv, Options &opts)
{
  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];
    const bool hasValue = i + 1 < argc;
    if ((arg == "--color" || arg == "--first-color" || arg == "--comm"
            || arg == "--first-comm" || arg == "--background"
            || arg == "--expect-library" || arg == "--wrapped-library")
        && !hasValue) {
      fprintf(stderr, "missing value for '%s'\n", arg.c_str());
      return false;
    }
    if (arg == "--color") {
      if (!parseColorType(argv[++i], opts.colorType))
        return false;
    } else if (arg == "--first-color") {
      ANARIDataType type;
      if (!parseColorType(argv[++i], type))
        return false;
      opts.firstColorType = type;
    } else if (arg == "--depth")
      opts.depth = true;
    else if (arg == "--ids")
      opts.ids = true;
    else if (arg == "--resize")
      opts.resize = true;
    else if (arg == "--unset-all")
      opts.unsetAll = true;
    else if (arg == "--uncommitted")
      opts.uncommitted = true;
    else if (arg == "--callback")
      opts.callback = true;
    else if (arg == "--reenter")
      opts.reenter = true;
    else if (arg == "--wrong-types")
      opts.wrongTypes = true;
    else if (arg == "--invert-check")
      opts.invertCheck = true;
    else if (arg == "--fail-last-rank")
      opts.failLastRank = true;
    else if (arg == "--comm") {
      if (!parseComm(argv[++i], opts.comm))
        return false;
    } else if (arg == "--first-comm") {
      CommSplit split;
      if (!parseComm(argv[++i], split))
        return false;
      opts.firstComm = split;
    } else if (arg == "--background") {
      if (!parseBackground(argv[++i], opts.background))
        return false;
    } else if (arg == "--expect-library")
      opts.expectLibrary = argv[++i];
    else if (arg == "--wrapped-library")
      opts.wrappedLibrary = argv[++i];
    else {
      fprintf(stderr, "unknown argument '%s'\n", arg.c_str());
      return false;
    }
  }
  return true;
}

// MPI_COMM_WORLD, or a new communicator for this rank's part of it. Every
// call splits anew, so two layouts with the same split have different but
// congruent communicators.
Layout makeLayout(CommSplit split)
{
  Layout l;
  l.split = split;
  int worldRank = 0, worldSize = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &worldRank);
  MPI_Comm_size(MPI_COMM_WORLD, &worldSize);
  if (split != CommSplit::World) {
    const int part = split == CommSplit::Halves ? worldRank / (worldSize / 2)
                                                : worldRank % 2;
    MPI_Comm_split(MPI_COMM_WORLD, part, worldRank, &l.comm);
  }
  MPI_Comm_rank(l.comm, &l.rank);
  MPI_Comm_size(l.comm, &l.size);
  l.worldRanks.resize(l.size);
  MPI_Allgather(
      &worldRank, 1, MPI_INT, l.worldRanks.data(), 1, MPI_INT, l.comm);
  return l;
}

void freeLayout(Layout &l)
{
  if (l.split != CommSplit::World)
    MPI_Comm_free(&l.comm);
}

// x position of rank r's sphere, in world space
float sphereX(int r, int size)
{
  return size == 1 ? 0.f : -1.f + 2.f * r / (size - 1);
}

// pixel column of a world-space x under the orthographic camera
int pixelX(float x)
{
  return int((x / (CAMERA_HEIGHT * ASPECT) + 0.5f) * IMAGE_SIZE.x);
}

std::string canonicalPath(const std::string &path)
{
  char buf[PATH_MAX];
  return realpath(path.c_str(), buf) ? std::string(buf) : path;
}

// Path of the libanari_library_ptc mapped into this process, or "" if none
// (or not on Linux).
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

// Color of rank r of the layout's communicator
float3 sphereColor(const Layout &l, int r)
{
  return COLORS[l.worldRanks[r] % NUM_COLORS];
}

// Backgrounds by world rank, as premultiplied linear RGB and alpha (the
// convention ptc composites in). The values are far enough apart that
// blending the sRGB-encoded values, or treating them as straight alpha, is
// off by many LSBs.
const float4 BLEND_BACKGROUNDS[] = {{0.20f, 0.02f, 0.00f, 0.40f},
    {0.00f, 0.15f, 0.05f, 0.30f},
    {0.05f, 0.00f, 0.25f, 0.50f},
    {0.10f, 0.10f, 0.00f, 0.25f}};
// rank 0's has a dark channel, where sRGB and linear 8-bit codes differ most
const float4 OPAQUE_BACKGROUNDS[] = {{0.002f, 0.18f, 0.60f, 1.f},
    {0.50f, 0.50f, 0.00f, 1.f},
    {0.00f, 0.50f, 0.50f, 1.f},
    {0.50f, 0.00f, 0.50f, 1.f}};
const int NUM_BACKGROUNDS =
    int(sizeof(BLEND_BACKGROUNDS) / sizeof(BLEND_BACKGROUNDS[0]));

float4 background(Background bg, int worldRank)
{
  switch (bg) {
  case Background::Blend:
    return BLEND_BACKGROUNDS[worldRank % NUM_BACKGROUNDS];
  case Background::Opaque:
    return OPAQUE_BACKGROUNDS[worldRank % NUM_BACKGROUNDS];
  default:
    return float4(0.f);
  }
}

// This rank's spheres, placed by its rank in the layout's communicator
// Primitive index of rank r's tie sphere (--ids): after its visible sphere
// and, on all but the last rank, the hidden one
uint32_t tiePrimitive(int r, int size)
{
  return r == size - 1 ? 1u : 2u;
}

anari::World buildWorld(anari::Device d, const Layout &l, bool tie)
{
  const int rank = l.rank;
  const int size = l.size;
  // [0]: this rank's visible sphere; [1]: hidden behind the last rank's
  // sphere, at a different depth on each rank; then the tie sphere
  std::vector<float3> pos = {{sphereX(rank, size), 0.f, 0.f}};
  if (rank != size - 1)
    pos.push_back({sphereX(size - 1, size), 0.f, -1.f - 0.5f * rank});
  if (tie)
    pos.push_back({TIE_X, 0.f, 0.f});
  const int numSpheres = int(pos.size());
  float3 col = sphereColor(l, rank);
  auto geom = anari::newObject<anari::Geometry>(d, "sphere");
  anari::setParameterArray1D(
      d, geom, "vertex.position", pos.data(), numSpheres);
  anari::setParameter(d, geom, "radius", SPHERE_RADIUS);
  anari::commitParameters(d, geom);
  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", col);
  anari::commitParameters(d, mat);
  auto surf = anari::newObject<anari::Surface>(d);
  anari::setParameter(d, surf, "id", surfaceId(l.worldRanks[rank]));
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
  return world;
}

Scene buildScene(anari::Device d, anari::World w, Background bg)
{
  int worldRank = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &worldRank);

  auto cam = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, cam, "position", float3(0, 0, CAMERA_Z));
  anari::setParameter(d, cam, "direction", float3(0, 0, -1));
  anari::setParameter(d, cam, "up", float3(0, 1, 0));
  anari::setParameter(d, cam, "height", CAMERA_HEIGHT);
  anari::setParameter(d, cam, "aspect", ASPECT);
  anari::commitParameters(d, cam);

  auto ren = anari::newObject<anari::Renderer>(d, "default");
  anari::setParameter(d, ren, "background", background(bg, worldRank));
  anari::setParameter(d, ren, "ambientRadiance", 1.f);
  anari::commitParameters(d, ren);

  return {w, cam, ren};
}

void releaseScene(anari::Device d, const Scene &s)
{
  anari::release(d, s.world);
  anari::release(d, s.camera);
  anari::release(d, s.renderer);
}

void renderAndWait(anari::Device d, anari::Frame frame)
{
  anari::render(d, frame);
  anari::wait(d, frame);
}

namespace {

// What mpiCommunicator points to. ptc may only read it until the frame's
// commit returns, so commitFrame() clobbers it afterwards.
MPI_Comm g_commParameter = MPI_COMM_NULL;

void setCommunicator(anari::Device d, anari::Frame frame, const Layout &l)
{
  g_commParameter = l.comm;
  anari::setParameter(d, frame, "mpiCommunicator", (void *)&g_commParameter);
}

} // namespace

void commitFrame(anari::Device d, anari::Frame frame)
{
  anari::commitParameters(d, frame);
  g_commParameter = MPI_COMM_NULL;
}

// Set every parameter of `state` (and the scene objects) on the frame.
void setAllParameters(anari::Device d,
    anari::Frame frame,
    const Scene &scene,
    const FrameState &state)
{
  anari::setParameter(d, frame, "world", scene.world);
  anari::setParameter(d, frame, "camera", scene.camera);
  anari::setParameter(d, frame, "renderer", scene.renderer);
  anari::setParameter(d, frame, "size", state.size);
  if (state.color != ANARI_UNKNOWN)
    anari::setParameter(d, frame, "channel.color", state.color);
  if (state.depth)
    anari::setParameter(d, frame, "channel.depth", ANARI_FLOAT32);
  if (state.ids) {
    for (const char *channel : ID_CHANNELS)
      anari::setParameter(d, frame, channel, ANARI_UINT32);
  }
  if (state.callback) {
    anari::setParameter(d,
        frame,
        "frameCompletionCallback",
        (ANARIFrameCompletionCallback)onFrameComplete);
    anari::setParameter(
        d, frame, "frameCompletionCallbackUserData", (void *)&g_callback);
  }
  if (state.layout->split != CommSplit::World)
    setCommunicator(d, frame, *state.layout);
}

// Move the frame from `prev` to `next`: set what changed and unset what went
// away, or with `unsetAll` start over from anariUnsetAllParameters. Commits.
void changeParameters(anari::Device d,
    anari::Frame frame,
    const Scene &scene,
    const FrameState &prev,
    const FrameState &next,
    bool unsetAll)
{
  if (unsetAll) {
    anari::unsetAllParameters(d, frame);
    setAllParameters(d, frame, scene, next);
  } else {
    if (next.size != prev.size)
      anari::setParameter(d, frame, "size", next.size);
    if (next.color == ANARI_UNKNOWN)
      anari::unsetParameter(d, frame, "channel.color");
    else if (next.color != prev.color)
      anari::setParameter(d, frame, "channel.color", next.color);
    if (!next.depth)
      anari::unsetParameter(d, frame, "channel.depth");
    else if (!prev.depth)
      anari::setParameter(d, frame, "channel.depth", ANARI_FLOAT32);
    for (const char *channel : ID_CHANNELS) {
      if (!next.ids)
        anari::unsetParameter(d, frame, channel);
      else if (!prev.ids)
        anari::setParameter(d, frame, channel, ANARI_UINT32);
    }
    if (next.layout->split != CommSplit::World)
      setCommunicator(d, frame, *next.layout);
    else if (prev.layout->split != CommSplit::World)
      anari::unsetParameter(d, frame, "mpiCommunicator");
  }
  commitFrame(d, frame);
}

} // namespace ptcsmoke
