// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Wrapped-library selection and load failure for the ptc device.
//
// Usage: ptcWrapped --scenario missing|fallback [--expect-library <path>]
//
//   missing   set the device parameter 'wrappedLibrary' to a library that
//             does not exist (PTC_ANARI_LIBRARY names one that does: the
//             parameter wins) and commit the device: that reports a
//             FATAL_ERROR, after which every API call reports an ERROR or
//             returns nothing, but none crashes. Object creation returns
//             null. ptc's own properties and extensions are still reported.
//   fallback  PTC_ANARI_LIBRARY names a library that does not exist and no
//             parameter is set: ptc falls back to helide and says so with a
//             WARNING, both for the library's extension list and for the
//             device, which then works.
//
// Every rank runs the scenario; every rank exits non-zero if any rank
// recorded a failure.

// anari
#include <anari/anari.h>
// mpi
#include <mpi.h>
// std
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {

const char *const MISSING_LIBRARY = "ptcTestNoSuchLibrary";
const char *const MPI_EXTENSION = "ANARI_KHR_DATA_PARALLEL_MPI";
const char *const CALLBACK_EXTENSION = "ANARI_KHR_FRAME_COMPLETION_CALLBACK";
const int32_t PTC_VERSION =
    PTC_VERSION_MAJOR * 1000 + PTC_VERSION_MINOR * 100 + PTC_VERSION_PATCH;

int g_fatals = 0;
int g_errors = 0; // ERROR only, not FATAL_ERROR
int g_warnings = 0; // WARNING only
std::string g_messages; // every message at WARNING or worse since the last
                        // clearMessages()

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity sev,
    ANARIStatusCode,
    const char *msg)
{
  if (sev > ANARI_SEVERITY_WARNING)
    return;
  fprintf(stderr, "[anari] %s\n", msg);
  g_messages += std::string(msg) + "\n";
  g_fatals += sev == ANARI_SEVERITY_FATAL_ERROR;
  g_errors += sev == ANARI_SEVERITY_ERROR;
  g_warnings += sev == ANARI_SEVERITY_WARNING;
}

void clearMessages()
{
  g_fatals = g_errors = g_warnings = 0;
  g_messages.clear();
}

bool messagesMention(const std::string &s)
{
  return g_messages.find(s) != std::string::npos;
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

struct Checker
{
  int rank;
  int failures{0};

  void expect(bool ok, const std::string &what);
};

void Checker::expect(bool ok, const std::string &what)
{
  if (!ok)
    printf("FAIL: rank %d: %s\n", rank, what.c_str());
  failures += !ok;
}

bool lists(const char *const *list, const char *name)
{
  for (auto e = list; e && *e; e++) {
    if (std::string(*e) == name)
      return true;
  }
  return false;
}

// Runs `call` and expects it to report an ERROR (and nothing worse)
template <typename F>
void expectError(Checker &c, const char *what, F &&call)
{
  clearMessages();
  call();
  c.expect(g_errors > 0 && g_fatals == 0,
      std::string(what) + " reported no ERROR (or a FATAL_ERROR)");
}

// Runs `create` and expects a null handle and an ERROR
template <typename F>
void expectNull(Checker &c, const char *what, F &&create)
{
  clearMessages();
  auto h = create();
  c.expect(h == nullptr, std::string(what) + " did not return null");
  c.expect(g_errors > 0, std::string(what) + " reported no ERROR");
}

void checkOwnProperties(Checker &c, ANARIDevice d)
{
  const char *const *exts = nullptr;
  c.expect(
      anariGetProperty(
          d, d, "extension", ANARI_STRING_LIST, &exts, sizeof(exts), ANARI_WAIT)
          && lists(exts, MPI_EXTENSION) && lists(exts, CALLBACK_EXTENSION),
      "device 'extension' lacks ptc's own extensions");
  int32_t version = -1;
  c.expect(
      anariGetProperty(
          d, d, "version", ANARI_INT32, &version, sizeof(version), ANARI_WAIT)
          && version == PTC_VERSION,
      "device 'version' is not ptc's");
}

int runMissing(ANARILibrary lib, int rank)
{
  Checker c{rank};
  ANARIDevice d = anariNewDevice(lib, "default");
  c.expect(d != nullptr, "anariNewDevice returned null");
  if (!d)
    return c.failures;

  // Nothing loads before the commit
  clearMessages();
  anariSetParameter(d, d, "wrappedLibrary", ANARI_STRING, MISSING_LIBRARY);
  c.expect(g_fatals == 0 && g_errors == 0,
      "setting 'wrappedLibrary' reported an error before the commit");

  clearMessages();
  anariCommitParameters(d, d);
  c.expect(g_fatals == 1,
      "committing a missing 'wrappedLibrary' reported "
          + std::to_string(g_fatals) + " FATAL_ERRORs, want 1");
  c.expect(messagesMention(MISSING_LIBRARY),
      "the load failure does not name the library");
  c.expect(!messagesMention("falling back"),
      "a missing 'wrappedLibrary' fell back to another library");
  c.expect(!messagesMention("no wrapped device"),
      "the device commit reported more than the load failure");

  // Object creation returns null and reports an ERROR
  expectNull(c, "anariNewArray1D", [&] {
    return anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_FLOAT32, 4);
  });
  expectNull(c, "anariNewArray2D", [&] {
    return anariNewArray2D(d, nullptr, nullptr, nullptr, ANARI_FLOAT32, 2, 2);
  });
  expectNull(c, "anariNewArray3D", [&] {
    return anariNewArray3D(
        d, nullptr, nullptr, nullptr, ANARI_FLOAT32, 2, 2, 2);
  });
  expectNull(
      c, "anariNewGeometry", [&] { return anariNewGeometry(d, "sphere"); });
  expectNull(
      c, "anariNewMaterial", [&] { return anariNewMaterial(d, "matte"); });
  expectNull(
      c, "anariNewSampler", [&] { return anariNewSampler(d, "image1D"); });
  expectNull(c, "anariNewSurface", [&] { return anariNewSurface(d); });
  expectNull(c, "anariNewSpatialField", [&] {
    return anariNewSpatialField(d, "structuredRegular");
  });
  expectNull(c, "anariNewVolume", [&] {
    return anariNewVolume(d, "transferFunction1D");
  });
  expectNull(
      c, "anariNewLight", [&] { return anariNewLight(d, "directional"); });
  expectNull(c, "anariNewGroup", [&] { return anariNewGroup(d); });
  expectNull(
      c, "anariNewInstance", [&] { return anariNewInstance(d, "transform"); });
  expectNull(c, "anariNewWorld", [&] { return anariNewWorld(d); });
  expectNull(
      c, "anariNewCamera", [&] { return anariNewCamera(d, "perspective"); });
  expectNull(
      c, "anariNewRenderer", [&] { return anariNewRenderer(d, "default"); });
  ANARIFrame frame = nullptr;
  expectNull(c, "anariNewFrame", [&] { return frame = anariNewFrame(d); });

  // Calls on the (null) handles the app got back report an ERROR and return
  expectError(c, "anariSetParameter", [&] {
    float f = 1.f;
    anariSetParameter(d, nullptr, "radius", ANARI_FLOAT32, &f);
  });
  expectError(c, "anariUnsetParameter", [&] {
    anariUnsetParameter(d, nullptr, "radius");
  });
  expectError(c, "anariUnsetAllParameters", [&] {
    anariUnsetAllParameters(d, nullptr);
  });
  expectError(
      c, "anariCommitParameters", [&] { anariCommitParameters(d, nullptr); });
  expectError(c, "anariRetain", [&] { anariRetain(d, nullptr); });
  expectError(c, "anariMapArray", [&] {
    c.expect(!anariMapArray(d, nullptr), "anariMapArray returned memory");
  });
  expectError(c, "anariUnmapArray", [&] { anariUnmapArray(d, nullptr); });
  expectError(c, "anariMapParameterArray1D", [&] {
    uint64_t stride = 0;
    c.expect(!anariMapParameterArray1D(
                 d, nullptr, "vertex.position", ANARI_FLOAT32_VEC3, 4, &stride),
        "anariMapParameterArray1D returned memory");
  });
  expectError(c, "anariUnmapParameterArray", [&] {
    anariUnmapParameterArray(d, nullptr, "vertex.position");
  });
  expectError(c, "anariRenderFrame", [&] { anariRenderFrame(d, frame); });
  expectError(c, "anariFrameReady", [&] {
    c.expect(!anariFrameReady(d, frame, ANARI_WAIT),
        "anariFrameReady on a null frame returned ready");
  });
  expectError(c, "anariDiscardFrame", [&] { anariDiscardFrame(d, frame); });
  expectError(c, "anariMapFrame", [&] {
    uint32_t w = 0, h = 0;
    ANARIDataType t = ANARI_UNKNOWN;
    c.expect(!anariMapFrame(d, frame, "channel.color", &w, &h, &t),
        "anariMapFrame on a null frame returned memory");
  });
  expectError(c, "anariUnmapFrame", [&] {
    anariUnmapFrame(d, frame, "channel.color");
  });
  expectError(c, "anariGetObjectSubtypes", [&] {
    c.expect(!anariGetObjectSubtypes(d, ANARI_GEOMETRY),
        "anariGetObjectSubtypes returned a list");
  });
  expectError(c, "anariGetObjectInfo", [&] {
    c.expect(!anariGetObjectInfo(
                 d, ANARI_FRAME, nullptr, "parameter", ANARI_PARAMETER_LIST),
        "anariGetObjectInfo returned info");
  });
  expectError(c, "anariGetParameterInfo", [&] {
    c.expect(!anariGetParameterInfo(d,
                 ANARI_GEOMETRY,
                 "sphere",
                 "radius",
                 ANARI_FLOAT32,
                 "required",
                 ANARI_BOOL),
        "anariGetParameterInfo returned info");
  });
  {
    // a property of the null object: anariGetProperty reports and returns 0
    int32_t v = 0;
    expectError(c, "anariGetProperty on an object", [&] {
      c.expect(
          !anariGetProperty(
              d, nullptr, "bounds", ANARI_INT32, &v, sizeof(v), ANARI_WAIT),
          "anariGetProperty on the null object returned a value");
    });
  }

  // ptc's own device state still works
  clearMessages();
  checkOwnProperties(c, d);
  c.expect(anariGetParameterInfo(d,
               ANARI_FRAME,
               nullptr,
               "mpiCommunicator",
               ANARI_VOID_POINTER,
               "description",
               ANARI_STRING)
          != nullptr,
      "frame 'mpiCommunicator' info missing after the load failure");
  c.expect(g_fatals == 0 && g_errors == 0,
      "querying ptc's own device state reported an error");

  // A later device commit doesn't load anything again: the new choice is
  // ignored with a WARNING, and nothing is reported as FATAL again
  clearMessages();
  anariSetParameter(d, d, "wrappedLibrary", ANARI_STRING, "helide");
  anariCommitParameters(d, d);
  c.expect(g_fatals == 0 && g_errors == 0,
      "a second device commit reported an error");
  c.expect(g_warnings == 1 && messagesMention("ignored"),
      "changing 'wrappedLibrary' too late was not reported as ignored");

  clearMessages();
  anariRelease(d, d);
  c.expect(
      g_fatals == 0 && g_errors == 0, "releasing the device reported an error");
  return c.failures;
}

int runFallback(ANARILibrary lib, int rank)
{
  Checker c{rank};

  // library-level extension list
  clearMessages();
  auto libExts = anariGetDeviceExtensions(lib, "default");
  c.expect(lists(libExts, MPI_EXTENSION) && lists(libExts, CALLBACK_EXTENSION),
      "library extension list lacks ptc's own extensions");
  c.expect(lists(libExts, "ANARI_KHR_GEOMETRY_SPHERE"),
      "library extension list lacks helide's extensions after the fallback");
  c.expect(g_warnings == 1 && messagesMention("helide"),
      "library extension list: fallback to helide not reported once as a "
      "WARNING");
  c.expect(g_fatals == 0, "library extension list reported a FATAL_ERROR");

  // device
  ANARIDevice d = anariNewDevice(lib, "default");
  c.expect(d != nullptr, "anariNewDevice returned null");
  if (!d)
    return c.failures;
  clearMessages();
  ANARIWorld w = anariNewWorld(d);
  c.expect(w != nullptr, "anariNewWorld returned null after the fallback");
  c.expect(g_warnings == 1 && messagesMention("helide"),
      "device: fallback to helide not reported once as a WARNING");
  c.expect(g_fatals == 0, "device reported a FATAL_ERROR");
  checkOwnProperties(c, d);
  anariRelease(d, w);
  anariRelease(d, d);
  return c.failures;
}

} // namespace

int main(int argc, char **argv)
{
  // The app owns MPI here, so that ptc never finalizes it
  MPI_Init(&argc, &argv);
  int rank = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  std::string scenario, expectLibrary;
  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];
    if (arg == "--scenario" && i + 1 < argc)
      scenario = argv[++i];
    else if (arg == "--expect-library" && i + 1 < argc)
      expectLibrary = argv[++i];
    else {
      fprintf(stderr, "unknown argument '%s'\n", arg.c_str());
      MPI_Finalize();
      return 2;
    }
  }
  if (scenario != "missing" && scenario != "fallback") {
    fprintf(stderr, "missing or unknown --scenario\n");
    MPI_Finalize();
    return 2;
  }

  int failures = 0;
  ANARILibrary lib = anariLoadLibrary("ptc", statusFunc, nullptr);
  if (!lib) {
    printf("FAIL: rank %d could not load the ptc library\n", rank);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  if (!expectLibrary.empty()
      && loadedPtcLibrary() != canonicalPath(expectLibrary)) {
    printf("FAIL: rank %d loaded ptc from '%s', expected '%s'\n",
        rank,
        loadedPtcLibrary().c_str(),
        expectLibrary.c_str());
    failures++;
  }

  failures +=
      scenario == "missing" ? runMissing(lib, rank) : runFallback(lib, rank);
  anariUnloadLibrary(lib);

  int total = 0;
  MPI_Allreduce(&failures, &total, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  if (rank == 0)
    printf(total ? "WRAPPED FAILED (%d)\n" : "WRAPPED OK\n", total);
  fflush(stdout);
  MPI_Finalize();
  return total ? 1 : 0;
}
