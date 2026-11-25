// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// guardKillHostOf <milliseconds> <command> [arguments...]
//
// Runs an unmodified program through the guard device and kills its host from
// outside, <milliseconds> after the first host appears. Succeeds if the
// program then ends by its own choice (any exit code) and leaves no host
// behind; fails if a signal ended it.

#include "TestSupport.h"

#include <sys/wait.h>
#include <unistd.h>

using namespace guard_test;

static const char *HOST = "anariGuardHost";

int main(int argc, char **argv)
{
  if (argc < 3) {
    std::fprintf(
        stderr, "usage: %s <milliseconds> <command> [args...]\n", argv[0]);
    return 2;
  }
  const int delay = std::atoi(argv[1]);

  const pid_t program = fork();
  CHECK(program >= 0);
  if (program == 0) {
    execvp(argv[2], argv + 2);
    std::perror(argv[2]);
    _exit(127);
  }

  // The first host may be a short-lived one answering an extension query, so
  // the one to kill is whichever is there after the delay.
  CHECK(waitFor([&] { return findChild(program, HOST) > 0; }));
  std::this_thread::sleep_for(std::chrono::milliseconds(delay));
  const pid_t host = findChild(program, HOST);
  CHECK(host > 0);
  CHECK(kill(host, SIGKILL) == 0);

  int status = 0;
  CHECK(waitpid(program, &status, 0) == program);
  if (WIFSIGNALED(status)) {
    std::fprintf(stderr,
        "FAILED: the program was ended by signal %d\n",
        WTERMSIG(status));
    return 1;
  }
  CHECK(WIFEXITED(status));
  CHECK(WEXITSTATUS(status) != 127);
  CHECK(waitFor([&] { return !processExists(host); }));
  std::printf(
      "host killed; the program exited with code %d\n", WEXITSTATUS(status));
  return 0;
}
