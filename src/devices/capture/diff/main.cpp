// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// anariCaptureDiff: compares two captures written by the 'capture' device.
// Exits 0 if they match, 1 if they differ, and 2 if an input can't be read or
// the tool is used wrongly.

#include "anari/ext/capture/CaptureDiff.h"
// std
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

void usage(FILE *out)
{
  std::fputs(
      "usage: anariCaptureDiff [options] <expected> <actual>\n"
      "\n"
      "Compares two captures: <expected> is the baseline, <actual> what to\n"
      "check against it. Exits 0 if they match, 1 if they differ, 2 on an\n"
      "unreadable input or a bad command line.\n"
      "\n"
      "options:\n"
      "  --abs-tol <x>          floats match within x of each other\n"
      "  --rel-tol <x>          floats match within x times their magnitude\n"
      "  --ulps <n>             floats match within n representable values\n"
      "  --unordered <path>     compare the object array at <path> (such as\n"
      "                         world.surface) as unordered; repeatable\n"
      "  --max-differences <n>  list at most n differences (default 50)\n"
      "  --quiet                print nothing\n"
      "  -h, --help             this text\n",
      out);
}

} // namespace

int main(int argc, char **argv)
{
  using namespace anari::capture_diff;

  DiffOptions options;
  bool quiet = false;
  std::string paths[2];
  int numPaths = 0;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto value = [&](const char *name) -> const char * {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "anariCaptureDiff: %s needs a value\n", name);
        std::exit(2);
      }
      return argv[++i];
    };
    if (arg == "-h" || arg == "--help") {
      usage(stdout);
      return 0;
    } else if (arg == "--abs-tol") {
      options.tolerance.absolute = std::atof(value("--abs-tol"));
    } else if (arg == "--rel-tol") {
      options.tolerance.relative = std::atof(value("--rel-tol"));
    } else if (arg == "--ulps") {
      options.tolerance.ulps =
          uint32_t(std::strtoul(value("--ulps"), nullptr, 10));
    } else if (arg == "--unordered") {
      options.unorderedPaths.push_back(value("--unordered"));
    } else if (arg == "--max-differences") {
      options.maxDifferences =
          size_t(std::strtoull(value("--max-differences"), nullptr, 10));
    } else if (arg == "--quiet") {
      quiet = true;
    } else if (arg.rfind("--", 0) == 0) {
      std::fprintf(
          stderr, "anariCaptureDiff: unknown option '%s'\n", arg.c_str());
      usage(stderr);
      return 2;
    } else if (numPaths < 2) {
      paths[numPaths++] = arg;
    } else {
      std::fprintf(stderr, "anariCaptureDiff: too many arguments\n");
      usage(stderr);
      return 2;
    }
  }
  if (numPaths != 2) {
    usage(stderr);
    return 2;
  }

  const DiffResult result = diffCaptureFiles(paths[0], paths[1], options);
  if (!quiet) {
    const std::string report = result.report();
    std::fputs(
        report.c_str(), result.outcome == Outcome::INVALID ? stderr : stdout);
  }
  switch (result.outcome) {
  case Outcome::MATCH:
    return 0;
  case Outcome::DIFFER:
    return 1;
  default:
    return 2;
  }
}
