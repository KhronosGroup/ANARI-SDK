// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Isolation.h"
// std
#include <algorithm>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <cerrno>
#include <cstring>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
extern char **environ;
#endif

namespace anari {
namespace cts {

namespace {

ProcessResult timedOut(std::chrono::milliseconds timeout)
{
  ProcessResult r;
  r.status = ProcessResult::Status::TimedOut;
  r.detail = "still running after " + durationText(timeout) + "; killed it";
  return r;
}

#ifdef _WIN32

// Quotes one argument for CreateProcess() the way the MSVC runtime parses it
// back: backslashes are literal unless they precede a double quote.
std::wstring quoteArgument(const std::wstring &arg)
{
  if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos)
    return arg;
  std::wstring out = L"\"";
  size_t backslashes = 0;
  for (wchar_t c : arg) {
    if (c == L'\\') {
      backslashes++;
      continue;
    }
    if (c == L'"')
      out.append(backslashes * 2 + 1, L'\\');
    else
      out.append(backslashes, L'\\');
    backslashes = 0;
    out.push_back(c);
  }
  out.append(backslashes * 2, L'\\');
  out.push_back(L'"');
  return out;
}

#endif

} // namespace

std::string durationText(std::chrono::milliseconds t)
{
  const auto ms = t.count();
  if (ms % 1000 == 0)
    return std::to_string(ms / 1000) + " s";
  return std::to_string(ms) + " ms";
}

ProcessResult runProcess(const std::filesystem::path &executable,
    const std::vector<std::string> &args,
    std::chrono::milliseconds timeout)
{
  ProcessResult result;
#ifdef _WIN32
  std::wstring commandLine = quoteArgument(executable.wstring());
  for (const auto &a : args)
    commandLine += L" " + quoteArgument(std::filesystem::path(a).wstring());

  // Give the child this process's standard streams, also when they are pipes
  // or files (ctest, CI).
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  PROCESS_INFORMATION info{};
  if (!CreateProcessW(executable.wstring().c_str(),
          commandLine.data(),
          nullptr,
          nullptr,
          TRUE,
          0,
          nullptr,
          nullptr,
          &startup,
          &info)) {
    result.detail =
        "CreateProcess failed (error " + std::to_string(GetLastError()) + ")";
    return result;
  }
  CloseHandle(info.hThread);

  // (INFINITE is 0xFFFFFFFF ms; stay below it.)
  const auto waitMs =
      static_cast<DWORD>(std::min<long long>(timeout.count(), INFINITE - 1ll));
  const DWORD waited = WaitForSingleObject(info.hProcess, waitMs);
  if (waited != WAIT_OBJECT_0) {
    TerminateProcess(info.hProcess, 1);
    WaitForSingleObject(info.hProcess, INFINITE);
    CloseHandle(info.hProcess);
    if (waited == WAIT_TIMEOUT)
      return timedOut(timeout);
    result.status = ProcessResult::Status::Crashed;
    result.detail = "waiting for it failed (error "
        + std::to_string(GetLastError()) + "); killed it";
    return result;
  }

  DWORD code = 0;
  GetExitCodeProcess(info.hProcess, &code);
  CloseHandle(info.hProcess);
  // Unhandled exceptions end a process with their NTSTATUS code
  // (0xC0000005 for an access violation, ...).
  if (code >= 0xC0000000u) {
    char hex[16];
    std::snprintf(
        hex, sizeof(hex), "0x%08lX", static_cast<unsigned long>(code));
    result.status = ProcessResult::Status::Crashed;
    result.detail = std::string("crashed with exception ") + hex;
    return result;
  }
  result.status = ProcessResult::Status::Exited;
  result.exitCode = static_cast<int>(code);
  if (code != 0)
    result.detail = "exited with code " + std::to_string(code);
  return result;
#else
  const std::string exe = executable.string();
  std::vector<char *> argv;
  argv.push_back(const_cast<char *>(exe.c_str()));
  for (const auto &a : args)
    argv.push_back(const_cast<char *>(a.c_str()));
  argv.push_back(nullptr);

  pid_t pid = 0;
  const int err =
      posix_spawn(&pid, exe.c_str(), nullptr, nullptr, argv.data(), environ);
  if (err != 0) {
    result.detail = std::string("posix_spawn failed: ") + std::strerror(err);
    return result;
  }

  // Poll: POSIX has no wait-with-timeout for a child process.
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  int status = 0;
  for (;;) {
    const pid_t done = waitpid(pid, &status, WNOHANG);
    if (done == pid)
      break;
    if (done < 0 && errno != EINTR) {
      result.detail = std::string("waitpid failed: ") + std::strerror(errno);
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      return result;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      kill(pid, SIGKILL);
      while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
      }
      return timedOut(timeout);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  if (WIFSIGNALED(status)) {
    const int sig = WTERMSIG(status);
    result.status = ProcessResult::Status::Crashed;
    result.detail =
        "killed by signal " + std::to_string(sig) + " (" + strsignal(sig) + ")";
    return result;
  }
  result.status = ProcessResult::Status::Exited;
  result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  if (result.exitCode != 0)
    result.detail = "exited with code " + std::to_string(result.exitCode);
  return result;
#endif
}

std::filesystem::path currentExecutable()
{
#if defined(_WIN32)
  std::wstring path(MAX_PATH, L'\0');
  for (;;) {
    const DWORD n = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (n == 0)
      return {};
    if (n < path.size()) {
      path.resize(n);
      return std::filesystem::path(path);
    }
    path.resize(path.size() * 2);
  }
#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string path(size, '\0');
  if (_NSGetExecutablePath(path.data(), &size) != 0)
    return {};
  path.resize(std::strlen(path.c_str()));
  std::error_code ec;
  auto canonical = std::filesystem::canonical(path, ec);
  return ec ? std::filesystem::path(path) : canonical;
#else
  std::error_code ec;
  auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
  return ec ? std::filesystem::path() : path;
#endif
}

ProcessCaseIsolation::ProcessCaseIsolation(
    std::filesystem::path executable, std::vector<std::string> args)
    : m_executable(std::move(executable)), m_args(std::move(args))
{}

ProcessResult ProcessCaseIsolation::run(
    const Case &c, std::chrono::milliseconds timeout)
{
  auto args = m_args;
  args.push_back("--isolated-case");
  args.push_back(c.qualifiedId());
  return runProcess(m_executable, args, timeout);
}

} // namespace cts
} // namespace anari
