// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Device diagnostics checks of ptcSmoke: extension and property reporting, and
// warnings about wrong-typed parameters.

// anariGetDeviceExtensionStruct() is implemented in this translation unit
#define ANARI_EXTENSION_UTILITY_IMPL

// ptc
#include "SmokeHarness.h"
// std
#include <cstdio>
#include <set>
#include <utility>

namespace ptcsmoke {

namespace {

// Spelling used by the SDK's extension lists and anari_extension_utility.h
const char *const MPI_EXTENSION = "ANARI_KHR_DATA_PARALLEL_MPI";
// ptc implements this one itself, so it lists it whatever it wraps
const char *const CALLBACK_EXTENSION = "ANARI_KHR_FRAME_COMPLETION_CALLBACK";

// ptc's version (from CMake), reported as its device property 'version'
const int32_t PTC_VERSION =
    PTC_VERSION_MAJOR * 1000 + PTC_VERSION_MINOR * 100 + PTC_VERSION_PATCH;

// Checks that a null-terminated extension list holds each extension ptc
// implements itself exactly once, and has no duplicates.
void checkOwnExtensions(Checker &c, const char *const *list, const char *source)
{
  int mpi = 0, callback = 0;
  std::set<std::string> seen;
  for (auto e = list; e && *e; e++) {
    c.expect(seen.insert(*e).second,
        std::string(source) + " lists '" + *e + "' twice");
    mpi += std::string(*e) == MPI_EXTENSION;
    callback += std::string(*e) == CALLBACK_EXTENSION;
  }
  c.expect(mpi == 1, std::string(source) + " lacks " + MPI_EXTENSION);
  c.expect(callback == 1, std::string(source) + " lacks " + CALLBACK_EXTENSION);
}

template <typename T>
bool getDeviceProperty(
    anari::Device d, const char *name, ANARIDataType type, T &out)
{
  return anariGetProperty(d, d, name, type, &out, sizeof(out), ANARI_WAIT);
}

} // namespace

// All ranks: KHR_DATA_PARALLEL_MPI is reported everywhere an application can
// look for it, and the required device properties have valid values.
// anariGetProperty is collective under KHR_DATA_PARALLEL_MPI, so every rank
// makes the same calls.
int checkDeviceConformance(anari::Library lib, anari::Device d, int rank)
{
  Checker c{rank};

  const char *const *exts = nullptr;
  c.expect(getDeviceProperty(d, "extension", ANARI_STRING_LIST, exts) && exts,
      "device property 'extension' not reported");
  checkOwnExtensions(c, exts, "device property 'extension'");

  int32_t version = -1, major = -1, minor = -1;
  c.expect(getDeviceProperty(d, "version", ANARI_INT32, version)
          && version == PTC_VERSION,
      "device property 'version' is not ptc's (got " + std::to_string(version)
          + ", want " + std::to_string(PTC_VERSION) + ")");
  c.expect(getDeviceProperty(d, "version.major", ANARI_INT32, major)
          && major == PTC_VERSION_MAJOR,
      "device property 'version.major' is not ptc's");
  c.expect(getDeviceProperty(d, "anariVersion.major", ANARI_INT32, major)
          && getDeviceProperty(d, "anariVersion.minor", ANARI_INT32, minor)
          && major == 1 && minor >= 1,
      "device property 'anariVersion' is not 1.1 or later (got "
          + std::to_string(major) + "." + std::to_string(minor) + ")");
  uint64_t maxIndex = 0;
  c.expect(getDeviceProperty(d, "geometryMaxIndex", ANARI_UINT64, maxIndex)
          && maxIndex > 0,
      "device property 'geometryMaxIndex' not reported");

  auto libExts = anariGetDeviceExtensions(lib, "default");
  checkOwnExtensions(c, libExts, "library extension list");

  auto extStruct = anari::extension::getDeviceExtensionStruct(lib, "default");
  c.expect(extStruct.ANARI_KHR_DATA_PARALLEL_MPI,
      "anariGetDeviceExtensionStruct does not report the MPI extension");

  auto info = [&](const char *infoName, ANARIDataType infoType) {
    return anariGetParameterInfo(d,
        ANARI_FRAME,
        nullptr,
        "mpiCommunicator",
        ANARI_VOID_POINTER,
        infoName,
        infoType);
  };
  auto *required = (const int32_t *)info("required", ANARI_BOOL);
  c.expect(required && !*required,
      "parameter info 'required' missing for frame 'mpiCommunicator'");
  c.expect(info("description", ANARI_STRING),
      "parameter info 'description' missing for frame 'mpiCommunicator'");
  auto *source = (const char *)info("sourceExtension", ANARI_STRING);
  c.expect(source && std::string(source) == "KHR_DATA_PARALLEL_MPI",
      "parameter info 'sourceExtension' wrong for frame 'mpiCommunicator'");

  bool listed = false;
  auto *params = (const ANARIParameter *)anariGetObjectInfo(
      d, ANARI_FRAME, nullptr, "parameter", ANARI_PARAMETER_LIST);
  for (auto p = params; p && p->name; p++) {
    listed |= std::string(p->name) == "mpiCommunicator"
        && p->type == ANARI_VOID_POINTER;
  }
  c.expect(listed, "frame parameter list lacks 'mpiCommunicator'");

  return c.failures;
}

// --wrong-types, every rank: on a frame of its own, each parameter ptc
// tracks, set with the wrong type, is not warned about when set, but once,
// by name, at the next commit, and not again at the commit after that.
int checkWrongTypes(anari::Device d, int rank)
{
  Checker c{rank};
  auto frame = anari::newObject<anari::Frame>(d);

  const float2 size(64.f, 64.f);
  const int32_t notAType = ANARI_UFIXED8_RGBA_SRGB;
  const int32_t notAComm = 0;
  const ANARIDataType float64 = ANARI_FLOAT64;
  const ANARIDataType float32 = ANARI_FLOAT32;
  const std::pair<const char *, std::string> wrong[] = {
      {"size", "ANARI_FLOAT32_VEC2"},
      {"channel.color", "ANARI_INT32"},
      {"channel.depth", "ANARI_FLOAT64"},
      {"channel.primitiveId", "ANARI_FLOAT32"},
      {"mpiCommunicator", "ANARI_INT32"},
      {"frameCompletionCallback", "ANARI_VOID_POINTER"},
      {"frameCompletionCallbackUserData", "ANARI_INT32"}};
  const size_t atSet = g_warningLog.size();
  anariSetParameter(d, frame, "size", ANARI_FLOAT32_VEC2, &size);
  anariSetParameter(d, frame, "channel.color", ANARI_INT32, &notAType);
  anariSetParameter(d, frame, "channel.depth", ANARI_DATA_TYPE, &float64);
  anariSetParameter(d, frame, "channel.primitiveId", ANARI_DATA_TYPE, &float32);
  anariSetParameter(d, frame, "mpiCommunicator", ANARI_INT32, &notAComm);
  anariSetParameter(
      d, frame, "frameCompletionCallback", ANARI_VOID_POINTER, nullptr);
  anariSetParameter(
      d, frame, "frameCompletionCallbackUserData", ANARI_INT32, &notAComm);
  c.expect(g_warningLog.size() == atSet,
      "setting wrong-typed parameters warned before the commit: "
          + (g_warningLog.empty() ? std::string() : g_warningLog.back()));

  const size_t atCommit = g_warningLog.size();
  anari::commitParameters(d, frame);
  for (const auto &[name, type] : wrong) {
    int count = 0;
    for (size_t i = atCommit; i < g_warningLog.size(); i++) {
      count += g_warningLog[i].find(std::string("'") + name + "'")
          != std::string::npos;
    }
    c.expect(count == 1,
        std::string("'") + name + "' set as " + type + " warned "
            + std::to_string(count) + " times at commit, want once");
  }

  const size_t atRecommit = g_warningLog.size();
  anari::commitParameters(d, frame);
  c.expect(g_warningLog.size() == atRecommit,
      "a second commit warned again: "
          + (g_warningLog.size() > atRecommit ? g_warningLog.back()
                                              : std::string()));
  anari::release(d, frame);

  if (!c.failures)
    printf("PASS: rank %d: wrong-typed parameters warned at commit\n", rank);
  return c.failures;
}

} // namespace ptcsmoke
