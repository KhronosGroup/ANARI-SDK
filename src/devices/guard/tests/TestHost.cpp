// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// guardTestHost: anariGuardHost with a fault hook, for the tests of device
// death. It ends itself at a chosen call when the environment says
//
//   ANARI_GUARD_HOST_FAULT=<call>:<nth>:<when>:<how>
//
//   <call>  the call's name: renderFrame, mapFrame, getProperty, release, ...
//           (releaseDevice for the device's own release, and hello for the
//           host's first message, before anything is read)
//   <nth>   which occurrence of that call, counted from 1
//   <when>  'before' the wrapped device is called, or 'after' it has returned
//           and before any reply is sent
//   <how>   segv, abort, kill (SIGKILL), exit (_exit(1)), or unreachable:
//           both channels are shut down and the host stays alive, stopped
//
// Without the variable it behaves as anariGuardHost does. The shipped host
// has none of this.

#include "host/Host.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

using anari_guard::Op;

namespace {

struct Name
{
  const char *name;
  Op op;
};

const Name NAMES[] = {{"newArray1D", Op::NEW_ARRAY1D},
    {"newArray2D", Op::NEW_ARRAY2D},
    {"newArray3D", Op::NEW_ARRAY3D},
    {"newGeometry", Op::NEW_GEOMETRY},
    {"newMaterial", Op::NEW_MATERIAL},
    {"newSampler", Op::NEW_SAMPLER},
    {"newSurface", Op::NEW_SURFACE},
    {"newSpatialField", Op::NEW_SPATIAL_FIELD},
    {"newVolume", Op::NEW_VOLUME},
    {"newLight", Op::NEW_LIGHT},
    {"newGroup", Op::NEW_GROUP},
    {"newInstance", Op::NEW_INSTANCE},
    {"newWorld", Op::NEW_WORLD},
    {"newCamera", Op::NEW_CAMERA},
    {"newRenderer", Op::NEW_RENDERER},
    {"newFrame", Op::NEW_FRAME},
    {"newObject", Op::NEW_OBJECT},
    {"setParameter", Op::SET_PARAMETER},
    {"unsetParameter", Op::UNSET_PARAMETER},
    {"unsetAllParameters", Op::UNSET_ALL_PARAMETERS},
    {"commitParameters", Op::COMMIT_PARAMETERS},
    {"release", Op::RELEASE},
    {"retain", Op::RETAIN},
    {"unmapArray", Op::UNMAP_ARRAY},
    {"getProperty", Op::GET_PROPERTY},
    {"getObjectSubtypes", Op::GET_OBJECT_SUBTYPES},
    {"getObjectInfo", Op::GET_OBJECT_INFO},
    {"getParameterInfo", Op::GET_PARAMETER_INFO},
    {"mapFrame", Op::FRAME_BUFFER_MAP},
    {"renderFrame", Op::RENDER_FRAME},
    {"discardFrame", Op::DISCARD_FRAME},
    {"releaseDevice", Op::RELEASE_DEVICE},
    // Never seen by the observer: main() looks after this one.
    {"hello", Op::HELLO}};

enum class How
{
  SEGV,
  ABORT,
  KILL,
  EXIT,
  UNREACHABLE
};

// The host's ends of the two channels
int channels[2] = {-1, -1};

[[noreturn]] void die(How how)
{
  // No core file, and no crash handler started, for a death that was asked
  // for.
  const rlimit none = {0, 0};
  setrlimit(RLIMIT_CORE, &none);
  prctl(PR_SET_DUMPABLE, 0);

  switch (how) {
  case How::SEGV:
    // Raised with the default disposition, so that a sanitizer's handler
    // does not turn the signal into an exit.
    std::signal(SIGSEGV, SIG_DFL);
    raise(SIGSEGV);
    break;
  case How::ABORT:
    std::signal(SIGABRT, SIG_DFL);
    std::abort();
  case How::KILL:
    kill(getpid(), SIGKILL);
    break;
  case How::EXIT:
    break;
  case How::UNREACHABLE:
    // The reader would end the host at the end-of-file a shutdown brings,
    // so the host is stopped first, every thread of it, and a child that
    // shares the two sockets shuts them down once it is.
    if (fork() == 0) {
      const std::string stat = "/proc/" + std::to_string(getppid()) + "/stat";
      for (int i = 0; i < 5000; i++) {
        char text[512] = {0};
        if (FILE *f = std::fopen(stat.c_str(), "r")) {
          const size_t n = std::fread(text, 1, sizeof(text) - 1, f);
          std::fclose(f);
          text[n] = '\0';
        }
        // "pid (comm) state ..."
        const char *state = std::strrchr(text, ')');
        if (state && state[1] == ' ' && state[2] == 'T')
          break;
        usleep(1000);
      }
      shutdown(channels[0], SHUT_RDWR);
      shutdown(channels[1], SHUT_RDWR);
      _exit(0);
    }
    raise(SIGSTOP);
    break; // only if someone continues it
  }
  _exit(1);
}

struct Fault : public anari_guard::CallObserver
{
  Op op{Op::HELLO};
  unsigned long nth{0};
  bool afterCall{false};
  How how{How::SEGV};

  // 'a:b:c:d', every part present and known.
  bool parse(const char *text)
  {
    std::string parts[4];
    int n = 0;
    for (const char *c = text; *c; ++c) {
      if (*c == ':') {
        if (++n == 4)
          return false;
      } else
        parts[n] += *c;
    }
    if (n != 3)
      return false;

    bool known = false;
    for (const Name &candidate : NAMES) {
      if (parts[0] == candidate.name) {
        op = candidate.op;
        known = true;
      }
    }
    if (!known)
      return false;

    char *end = nullptr;
    nth = std::strtoul(parts[1].c_str(), &end, 10);
    if (parts[1].empty() || *end != '\0' || nth == 0)
      return false;

    if (parts[2] == "after")
      afterCall = true;
    else if (parts[2] != "before")
      return false;

    if (parts[3] == "segv")
      how = How::SEGV;
    else if (parts[3] == "abort")
      how = How::ABORT;
    else if (parts[3] == "kill")
      how = How::KILL;
    else if (parts[3] == "exit")
      how = How::EXIT;
    else if (parts[3] == "unreachable")
      how = How::UNREACHABLE;
    else
      return false;
    return true;
  }

  // Both run on the host's worker, one call at a time.
  void before(Op seen) override
  {
    if (seen == op && ++m_seen == nth && !afterCall)
      die(how);
  }

  void after(Op seen) override
  {
    if (seen == op && m_seen == nth && afterCall)
      die(how);
  }

 private:
  unsigned long m_seen{0};
};

} // namespace

int main(int argc, char *argv[])
{
  if (argc != 3) {
    std::fprintf(stderr,
        "guardTestHost is started by the ANARI guard device, not by hand.\n");
    return 2;
  }

  const char *spec = std::getenv("ANARI_GUARD_HOST_FAULT");
  if (!spec || !*spec)
    return anari_guard::runHost(std::atoi(argv[1]), std::atoi(argv[2]));

  Fault fault;
  if (!fault.parse(spec)) {
    std::fprintf(stderr,
        "guardTestHost: ANARI_GUARD_HOST_FAULT='%s' is not "
        "<call>:<nth>:<before|after>:<segv|abort|kill|exit|unreachable>\n",
        spec);
    return 2;
  }
  channels[0] = std::atoi(argv[1]);
  channels[1] = std::atoi(argv[2]);
  if (fault.op == Op::HELLO)
    die(fault.how);
  return anari_guard::runHost(std::atoi(argv[1]), std::atoi(argv[2]), &fault);
}
