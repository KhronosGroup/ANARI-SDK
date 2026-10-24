// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Compares two captures written by the 'capture' device. The comparison walks
// both object graphs from the frame, pairing objects by their place in the
// graph (never by ID). Integers, strings, data types, subtypes and array sizes
// must match exactly; floats match within a tolerance. Every difference is
// reported with a path built from parameter names and array indices, like
//
//   world.surface[2].geometry.vertex.position[17]: expected (0.1, 0.2, 0.3),
//   got (0.1, 0.2, 0)
//
// This library does not depend on the device or on the ANARI headers.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace anari {
namespace capture_diff {

// A capture: its text, and the blobs the text refers to
struct Capture
{
  std::string text;
  // Blobs held in memory, by name (the name is the 'blob' value in the text)
  std::map<std::string, std::vector<uint8_t>> blobs;
  // For a blob that is not in 'blobs': the file <blobDirectory>/<name>.bin.
  // Empty for none.
  std::string blobDirectory;
};

// Reads the capture in the file 'path'. Its blobs are in the 'blobs'
// directory beside it. Returns false, and says why in 'error', if the file
// can't be read.
bool readCaptureFile(
    const std::string &path, Capture &capture, std::string &error);

// Two floats match if they are equal, or any of the following holds:
//  - they are within 'absolute' of each other,
//  - they are within 'relative' times the larger of their magnitudes,
//  - they are at most 'ulps' representable values apart (in the precision of
//    their type).
// NaNs match NaNs. With all three at zero, floats match exactly.
struct FloatTolerance
{
  double absolute{0.0};
  double relative{0.0};
  uint32_t ulps{0};
};

struct DiffOptions
{
  FloatTolerance tolerance;

  // Object-array parameters to compare as unordered (as multisets): their
  // paths from the frame, such as "world.surface". A segment of a path
  // without an index matches any index, so "world.instance.group.surface"
  // names the surfaces of every group of every instance; "[3]" matches one
  // index and "[*]" any.
  std::vector<std::string> unorderedPaths;

  // The most differences DiffResult::differences lists. All are counted.
  size_t maxDifferences{50};

  // The most differing elements of one array that are listed; the rest of
  // them are counted and summarized in one line.
  size_t maxIndicesPerArray{8};
};

struct Difference
{
  std::string path;
  std::string expected;
  std::string actual;
  // Instead of 'expected' and 'actual': what is different, in words
  std::string note;

  // "<path>: expected <expected>, got <actual>", or "<path>: <note>"
  std::string message() const;
};

enum class Outcome
{
  MATCH, // the captures are the same
  DIFFER, // they differ: see DiffResult::differences
  INVALID // a capture can't be read: see DiffResult::error
};

struct DiffResult
{
  Outcome outcome{Outcome::MATCH};
  // The first differences, up to DiffOptions::maxDifferences
  std::vector<Difference> differences;
  // How many differences there are in all
  size_t totalDifferences{0};
  // For INVALID: why
  std::string error;

  // The differences, a line each, and how many there are in all, or the error
  std::string report() const;
};

// 'expected' is the baseline and 'actual' the capture to check against it
DiffResult diffCaptures(const Capture &expected,
    const Capture &actual,
    const DiffOptions &options = {});

// Reads both files, then diffCaptures(). A file that can't be read makes the
// result INVALID.
DiffResult diffCaptureFiles(const std::string &expectedPath,
    const std::string &actualPath,
    const DiffOptions &options = {});

} // namespace capture_diff
} // namespace anari
