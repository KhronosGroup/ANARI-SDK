// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "host/Host.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char *argv[])
{
  if (argc != 3) {
    std::fprintf(stderr,
        "anariGuardHost is started by the ANARI guard device, not by hand.\n");
    return 2;
  }
  return anari_guard::runHost(std::atoi(argv[1]), std::atoi(argv[2]));
}
