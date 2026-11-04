// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Shared harness of the ptcSmoke test program (see ptcSmoke.cpp for what it
// does and its command line): the options, the communicator layout, the scene
// and frame helpers, the status callback's counters and the checker. The
// pixel checks, the frame completion callback checks and the device
// diagnostics checks live in their own files.

#pragma once

// anari
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// mpi
#include <mpi.h>
// std
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ptcsmoke {

using namespace anari::math;

// Renderer background of every rank (--background)
enum class Background
{
  None, // transparent black
  Blend, // semi-transparent, different on each rank
  Opaque // opaque, different on each rank
};

// How the ranks are split into the communicators frames composite over
enum class CommSplit
{
  World, // no split: mpiCommunicator unset
  Halves, // lower and upper half of MPI_COMM_WORLD
  Interleaved // even and odd ranks of MPI_COMM_WORLD
};

// The communicator a frame composites over, and this rank's place in it.
struct Layout
{
  CommSplit split{CommSplit::World};
  MPI_Comm comm{MPI_COMM_WORLD};
  int rank{0};
  int size{1};
  // world rank of each rank of `comm`; it picks that rank's color, so each
  // part of a split renders different ones
  std::vector<int> worldRanks;
};

// The frame parameters ptc tracks. color == ANARI_UNKNOWN means unset.
struct FrameState
{
  uint2 size;
  ANARIDataType color{ANARI_UNKNOWN};
  bool depth{false};
  bool ids{false};
  bool callback{false};
  const Layout *layout{nullptr};
};

struct Options
{
  ANARIDataType colorType{ANARI_UFIXED8_RGBA_SRGB};
  bool depth{false};
  bool ids{false};
  bool resize{false};
  std::optional<ANARIDataType> firstColorType;
  bool unsetAll{false};
  bool uncommitted{false};
  bool callback{false};
  bool reenter{false};
  bool invertCheck{false};
  bool failLastRank{false};
  bool wrongTypes{false};
  CommSplit comm{CommSplit::World};
  std::optional<CommSplit> firstComm;
  Background background{Background::None};
  std::string expectLibrary;
  std::string wrappedLibrary;

  bool twoFrames() const;
};

// Scene constants /////////////////////////////////////////////////////////////

inline const float3 COLORS[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}};
inline const uint2 IMAGE_SIZE(128, 64);
inline const float CAMERA_HEIGHT = 1.5f;
inline const float ASPECT = float(IMAGE_SIZE.x) / IMAGE_SIZE.y;
inline const int NUM_COLORS = int(sizeof(COLORS) / sizeof(COLORS[0]));
// Every sphere has radius 0.3 and its center at z=0; the camera is at z=5
// looking down -z, so the depth at a sphere's center pixel is 4.7.
inline const float SPHERE_RADIUS = 0.3f;
inline const float CAMERA_Z = 5.f;
// x of the tie sphere (--ids): on the center row, between the ranks' spheres
inline const float TIE_X = -0.5f;

// Channels ptc composites by taking the nearest fragment's value (UINT32)
inline const char *const ID_CHANNELS[] = {
    "channel.primitiveId", "channel.objectId", "channel.instanceId"};
// Channels ptc does not composite: mapping them is always refused
inline const char *const UNSUPPORTED_CHANNELS[] = {
    "channel.normal", "channel.albedo"};

// Status callback counters ////////////////////////////////////////////////////

// Status messages at WARNING or worse that mention channel.depth/color
extern int g_depthWarnings;
extern int g_colorWarnings;
// Status messages at ERROR or worse that mention channel.color
extern int g_colorErrors;
// Status messages at ERROR or worse
extern int g_errors;
// Status messages at WARNING or worse
extern int g_warnings;
// Status messages at ERROR or worse with ANARI_STATUS_INVALID_ARGUMENT that
// mention channel.color
extern int g_colorInvalidArguments;
// The last status message at WARNING or worse
extern std::string g_lastWarning;
// Every status message at WARNING or worse, in order
extern std::vector<std::string> g_warningLog;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity sev,
    ANARIStatusCode code,
    const char *msg);

// Count of failed conformance checks, printed with the rank that failed them.
struct Checker
{
  int rank;
  int failures{0};

  void expect(bool ok, const std::string &what);
};

// Options and layout //////////////////////////////////////////////////////////

bool parseArgs(int argc, char **argv, Options &opts);

// Color types ptc composites (or ANARI_UNKNOWN: no color)
bool compositable(ANARIDataType type);

// MPI_COMM_WORLD, or a new communicator for this rank's part of it. Every
// call splits anew, so two layouts with the same split have different but
// congruent communicators.
Layout makeLayout(CommSplit split);
void freeLayout(Layout &l);

// Scene ///////////////////////////////////////////////////////////////////////

// surface id (objectId) of the sphere of the rank with this world rank
uint32_t surfaceId(int worldRank);
// x position of rank r's sphere, in world space
float sphereX(int r, int size);
// pixel column of a world-space x under the orthographic camera
int pixelX(float x);
// Color of rank r of the layout's communicator
float3 sphereColor(const Layout &l, int r);
// Primitive index of rank r's tie sphere (--ids): after its visible sphere
// and, on all but the last rank, the hidden one
uint32_t tiePrimitive(int r, int size);
// Renderer background of the rank with this world rank
float4 background(Background bg, int worldRank);

// This rank's spheres, placed by its rank in the layout's communicator
anari::World buildWorld(anari::Device d, const Layout &l, bool tie);

// Objects the frame references; kept alive so they can be set again after
// anariUnsetAllParameters.
struct Scene
{
  anari::World world;
  anari::Camera camera;
  anari::Renderer renderer;
};

Scene buildScene(anari::Device d, anari::World w, Background bg);
void releaseScene(anari::Device d, const Scene &s);

// Frame ///////////////////////////////////////////////////////////////////////

void renderAndWait(anari::Device d, anari::Frame frame);

// Commit the frame's parameters; the communicator parameter points to memory
// ptc may only read until this returns.
void commitFrame(anari::Device d, anari::Frame frame);

// Set every parameter of `state` (and the scene objects) on the frame.
void setAllParameters(anari::Device d,
    anari::Frame frame,
    const Scene &scene,
    const FrameState &state);

// Move the frame from `prev` to `next`: set what changed and unset what went
// away, or with `unsetAll` start over from anariUnsetAllParameters. Commits.
void changeParameters(anari::Device d,
    anari::Frame frame,
    const Scene &scene,
    const FrameState &prev,
    const FrameState &next,
    bool unsetAll);

// Path of the libanari_library_ptc mapped into this process, or "" if none
// (or not on Linux).
std::string loadedPtcLibrary();
std::string canonicalPath(const std::string &path);

// Pixel checks (SmokeChecks.cpp) /////////////////////////////////////////////

// Rank 0 only: the frame has no channel.color, or its last render failed
// ('failed'), so mapping color returns nothing and reports why through the
// status callback; after a failure, the warning must say so.
int checkColorRefused(anari::Device d,
    anari::Frame frame,
    const char *which,
    bool failed = false);
// Rank 0 only, first of two frames: the color map has that frame's size and
// type (or is refused if it has no color).
int checkFirstFrame(
    anari::Device d, anari::Frame frame, const FrameState &state);
// Rank 0 of the layout only: probe the pixel at the center of each rank's
// sphere.
int checkColor(
    anari::Device d, anari::Frame frame, const Options &opts, const Layout &l);
// Rank 0 of the layout only, with --background: the corner pixel is the ranks'
// backgrounds blended in linear space.
int checkBackground(
    anari::Device d, anari::Frame frame, const Options &opts, const Layout &l);
// Rank 0 of the layout only, with --depth: the composited depth is the
// nearest hit across all ranks.
int checkDepth(anari::Device d, anari::Frame frame, int size);
// Rank 0 only, without --depth: mapping channel.depth is refused.
int checkDepthRefused(anari::Device d, anari::Frame frame);
// Rank 0 of the layout only, with --ids: each pixel's ids are the nearest
// fragment's.
int checkIds(anari::Device d, anari::Frame frame, const Layout &l);
// Rank 0 only: channels ptc does not composite are always refused, and the
// id channels are refused unless --ids enabled them.
int checkChannelsRefused(anari::Device d, anari::Frame frame, bool ids);
// Ranks other than the layout's rank 0: mapping and reading what is returned
// must not crash.
void touchNonRootMaps(anari::Device d, anari::Frame frame);

// Frame completion callback (SmokeCallbacks.cpp) /////////////////////////////

// Called by ptc once the composited frame is ready (--callback).
void onFrameComplete(const void *userData, ANARIDevice d, ANARIFrame f);

// What the callback expects, passed to it as its user data.
struct CallbackContext
{
  anari::Device device{nullptr};
  anari::Frame frame{nullptr};
  int rank{0};
  const Layout *layout{nullptr};
  const Options *opts{nullptr};
  // the committed frame state of the render in flight, for rank 0's check
  const FrameState *rendering{nullptr};
  bool finalFrame{false};
  // the final render fails; its ERROR must come before the callback
  bool renderFails{false};
  int errorsBeforeRender{0};
  // the callback may get null user data (only the user data is unset)
  bool expectNullUserData{false};
  int nullUserDataCalls{0};
  // callbacks in progress (more than one: ptc re-entered it)
  int depth{0};
  // --reenter: re-entrant calls were tried, from the first callback
  bool reentered{false};
  int calls{0};
  int failures{0};
};

extern CallbackContext g_callback;

// Every rank, with --callback: one call per render so far; then unset the
// callback, render again, and expect no further call.
int checkCallbackCalls(
    anari::Device d, anari::Frame frame, int rank, int renders);

// Device diagnostics (SmokeDiagnostics.cpp) //////////////////////////////////

// All ranks: KHR_DATA_PARALLEL_MPI is reported everywhere an application can
// look for it, and the required device properties have valid values.
int checkDeviceConformance(anari::Library lib, anari::Device d, int rank);

// --wrong-types, every rank: parameters set with the wrong type warn once, by
// name, at the next commit.
int checkWrongTypes(anari::Device d, int rank);

// Inlined definitions ////////////////////////////////////////////////////////

inline bool Options::twoFrames() const
{
  return resize || firstColorType || firstComm || unsetAll;
}

} // namespace ptcsmoke
