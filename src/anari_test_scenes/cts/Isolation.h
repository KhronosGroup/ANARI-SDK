// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Case.h"
// std
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace anari {
namespace cts {

// How a child process ended.
struct ProcessResult
{
  enum class Status
  {
    Exited, // returned from main or called exit(); see exitCode
    Crashed, // killed by a signal or an unhandled exception
    TimedOut, // still running at the timeout; it was killed
    NotStarted, // could not be started
  };

  Status status{Status::NotStarted};
  int exitCode{0}; // for Exited
  std::string detail; // human-readable, for anything but a clean exit
};

// Runs 'executable' with 'args' (not including argv[0]), inheriting this
// process's environment and standard streams, and waits for it for at most
// 'timeout', killing it if it's still running then.
ProcessResult runProcess(const std::filesystem::path &executable,
    const std::vector<std::string> &args,
    std::chrono::milliseconds timeout);

// "30 s", or "250 ms" if not a whole number of seconds.
std::string durationText(std::chrono::milliseconds t);

// The path of this process's executable, or an empty path if unknown.
std::filesystem::path currentExecutable();

// Runs a Case of a timed Test (TestDef::timeout) outside the runner's process,
// so a device that hangs or crashes on it can't take the run down (ADR-0009).
// The process that runs the Case writes its sidecar; the runner reads it.
struct CaseIsolation
{
  virtual ~CaseIsolation() = default;

  virtual ProcessResult run(
      const Case &c, std::chrono::milliseconds timeout) = 0;
};

// Runs each Case by starting 'executable' with 'args' plus
// `--isolated-case <Case::qualifiedId()>`: anariCts re-running itself with its
// own command line.
struct ProcessCaseIsolation : CaseIsolation
{
  ProcessCaseIsolation(
      std::filesystem::path executable, std::vector<std::string> args);

  ProcessResult run(const Case &c, std::chrono::milliseconds timeout) override;

 private:
  std::filesystem::path m_executable;
  std::vector<std::string> m_args;
};

} // namespace cts
} // namespace anari
