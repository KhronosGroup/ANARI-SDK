// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// guardRunFaulted <fault> <command> [arguments...]
//
// Runs an unmodified program through the guard device with a host that dies
// at <fault> (see TestHost.cpp). Succeeds if the program then ends by its own
// choice (any exit code), having printed exactly one fatal status message,
// which names the wrapped library, and nothing about falling back to another
// device, and leaves no host behind. Fails if a signal ended it.

#include "DeathTest.h"

#include <set>

#include <sys/wait.h>

using namespace guard_test;

static size_t count(const std::string &text, const std::string &part)
{
  size_t n = 0;
  for (auto at = text.find(part); at != std::string::npos;
       at = text.find(part, at + part.size()))
    n++;
  return n;
}

int main(int argc, char **argv)
{
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <fault> <command> [args...]\n", argv[0]);
    return 2;
  }
  setFault(argv[1]);
  const char *wrapped = std::getenv("ANARI_GUARD_WRAPPED_DEVICE");
  CHECK(wrapped);

  int output[2];
  CHECK(pipe(output) == 0);
  const pid_t program = fork();
  CHECK(program >= 0);
  if (program == 0) {
    dup2(output[1], STDOUT_FILENO);
    dup2(output[1], STDERR_FILENO);
    close(output[0]);
    close(output[1]);
    execvp(argv[2], argv + 2);
    std::perror(argv[2]);
    _exit(127);
  }
  close(output[1]);

  std::string log;
  std::thread reader([&] {
    char buffer[4096];
    for (;;) {
      const ssize_t n = read(output[0], buffer, sizeof(buffer));
      if (n <= 0)
        break;
      log.append(buffer, size_t(n));
    }
  });

  // Every host the program is seen to have, while it runs.
  std::set<pid_t> hosts;
  int status = 0;
  for (;;) {
    const pid_t host = findChild(program, TEST_HOST);
    if (host > 0)
      hosts.insert(host);
    const pid_t ended = waitpid(program, &status, WNOHANG);
    CHECK(ended >= 0);
    if (ended == program)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  reader.join();

  auto fail = [&](const char *why) {
    std::fprintf(stderr, "%s\nFAILED: %s\n", log.c_str(), why);
    std::exit(1);
  };

  if (WIFSIGNALED(status)) {
    std::fprintf(
        stderr, "the program was ended by signal %d\n", WTERMSIG(status));
    fail("ended by a signal");
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) == 127)
    fail("the program did not run");
  if (hosts.empty())
    fail("the program never had a host");
  if (count(log, "FATAL") != 1)
    fail("not exactly one fatal status message");
  if (log.find("'" + libraryOf(wrapped) + "'") == std::string::npos
      || log.find("the device is dead") == std::string::npos)
    fail(
        "the fatal message is not the guard device's, naming the wrapped library");
  if (log.find("falling back") != std::string::npos)
    fail("the program fell back to another device");
  for (pid_t host : hosts) {
    if (!waitFor([&] { return !processExists(host); }))
      fail("a host was left behind");
  }

  std::printf("the host died at %s; the program exited with code %d\n",
      argv[1],
      WEXITSTATUS(status));
  return 0;
}
