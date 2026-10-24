// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Tests of the capture diff through its public interface: pairs of capture
// texts in, the differences out, and the command-line tool's exit codes.

#include "catch.hpp"

#include <anari/ext/capture/CaptureDiff.h>
// std
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace {

namespace diff = anari::capture_diff;

// A small capture: a camera, and a world of two surfaces, one over a triangle
// and one over a sphere. The pieces tests change are replaced by name.
std::string makeCapture(
    const std::vector<std::pair<std::string, std::string>> &replacements = {})
{
  std::string text =
      "anari-capture 1\n"
      "\n"
      "device {\n"
      "  extensions ANARI_STRING_LIST []\n"
      "}\n"
      "\n"
      "frame@0 {\n"
      "  camera ANARI_CAMERA camera@0\n"
      "  size ANARI_UINT32_VEC2 (2, 2)\n"
      "  world ANARI_WORLD world@0\n"
      "}\n"
      "\n"
      "camera@0 \"perspective\" {\n"
      "  fovy ANARI_FLOAT32 0.5\n"
      "  position ANARI_FLOAT32_VEC3 (0.1, 0.2, 0.3)\n"
      "}\n"
      "\n"
      "world@0 {\n"
      "  surface ANARI_ARRAY1D array1d@0\n"
      "}\n"
      "\n"
      "array1d@0 {\n"
      "  elementType ANARI_SURFACE\n"
      "  dimensions (2)\n"
      "  data [\n"
      "    surface@0\n"
      "    surface@1\n"
      "  ]\n"
      "}\n"
      "\n"
      "surface@0 {\n"
      "  geometry ANARI_GEOMETRY geometry@0\n"
      "}\n"
      "\n"
      "geometry@0 \"triangle\" {\n"
      "  vertex.position ANARI_ARRAY1D array1d@1\n"
      "}\n"
      "\n"
      "array1d@1 {\n"
      "  elementType ANARI_FLOAT32_VEC3\n"
      "  dimensions (2)\n"
      "  data [\n"
      "    (0, 0, 0)\n"
      "    (1, 0, 0)\n"
      "  ]\n"
      "}\n"
      "\n"
      "surface@1 {\n"
      "  geometry ANARI_GEOMETRY geometry@1\n"
      "}\n"
      "\n"
      "geometry@1 \"sphere\" {\n"
      "  radius ANARI_FLOAT32 2\n"
      "}\n";
  for (const auto &r : replacements) {
    const auto at = text.find(r.first);
    REQUIRE(at != std::string::npos);
    text.replace(at, r.first.size(), r.second);
  }
  return text;
}

diff::Capture inMemory(const std::string &text)
{
  diff::Capture c;
  c.text = text;
  return c;
}

diff::DiffResult compare(const std::string &expected,
    const std::string &actual,
    const diff::DiffOptions &options = {})
{
  return diff::diffCaptures(inMemory(expected), inMemory(actual), options);
}

// The reported differences, one line each
std::vector<std::string> lines(const diff::DiffResult &r)
{
  std::vector<std::string> out;
  for (const auto &d : r.differences)
    out.push_back(d.message());
  return out;
}

} // namespace

SCENARIO("identical captures do not differ", "[capture_diff]")
{
  const auto result = compare(makeCapture(), makeCapture());
  REQUIRE(result.outcome == diff::Outcome::MATCH);
  REQUIRE(result.differences.empty());
  REQUIRE(result.totalDifferences == 0);
}

SCENARIO(
    "integers, strings, types and structure match exactly", "[capture_diff]")
{
  WHEN("an integer vector differs")
  {
    const auto result =
        compare(makeCapture(), makeCapture({{"(2, 2)", "(2, 3)"}}));

    THEN("the difference is reported by its path, with both values")
    {
      REQUIRE(result.outcome == diff::Outcome::DIFFER);
      REQUIRE(lines(result)
          == std::vector<std::string>{"size: expected (2, 2), got (2, 3)"});
    }
  }

  WHEN("a parameter has another type")
  {
    const auto result = compare(makeCapture(),
        makeCapture({{"fovy ANARI_FLOAT32 0.5", "fovy ANARI_FLOAT64 0.5"}}));

    THEN("the type difference is reported, not the value")
    {
      REQUIRE(lines(result)
          == std::vector<std::string>{"camera.fovy: expected type "
                                      "ANARI_FLOAT32, got ANARI_FLOAT64"});
    }
  }

  WHEN("an object has another subtype")
  {
    const auto result = compare(makeCapture(),
        makeCapture(
            {{"camera@0 \"perspective\"", "camera@0 \"orthographic\""}}));

    THEN("it is reported at the object")
    {
      REQUIRE(lines(result)
          == std::vector<std::string>{"camera.@subtype: expected "
                                      "\"perspective\", got \"orthographic\""});
    }
  }

  WHEN("a parameter is missing, and another is extra")
  {
    const auto result = compare(makeCapture(),
        makeCapture(
            {{"  fovy ANARI_FLOAT32 0.5\n", "  aspect ANARI_FLOAT32 1\n"}}));

    THEN("both are reported")
    {
      REQUIRE(lines(result)
          == std::vector<std::string>{
              "camera.aspect: expected <absent>, got ANARI_FLOAT32 1",
              "camera.fovy: expected ANARI_FLOAT32 0.5, got <absent>"});
    }
  }

  WHEN("an array has another size or element type")
  {
    const auto result = compare(makeCapture(),
        makeCapture({{"dimensions (2)\n  data [\n    (0, 0, 0)\n    (1, 0, 0)\n",
            "dimensions (3)\n  data [\n    (0, 0, 0)\n    (1, 0, 0)\n    (2, 0, 0)\n"}}));

    THEN("the dimensions are reported")
    {
      REQUIRE(lines(result)
          == std::vector<std::string>{"world.surface[0].geometry.vertex."
                                      "position.@dimensions: expected (2), "
                                      "got (3)"});
    }
  }

  WHEN("an object is shared in one capture and not in the other")
  {
    // surface@1 refers to geometry@0 instead of its own geometry
    const auto result = compare(makeCapture(),
        makeCapture({{"surface@1 {\n  geometry ANARI_GEOMETRY geometry@1",
            "surface@1 {\n  geometry ANARI_GEOMETRY geometry@0"}}));

    THEN("it is reported")
    {
      REQUIRE(result.outcome == diff::Outcome::DIFFER);
    }
  }
}

SCENARIO("floats match within the tolerance", "[capture_diff]")
{
  const auto actual = makeCapture({{"(0.1, 0.2, 0.3)", "(0.1, 0.2, 0.3004)"}});

  WHEN("there is no tolerance")
  {
    const auto result = compare(makeCapture(), actual);

    THEN("any difference is reported, with the whole value")
    {
      REQUIRE(lines(result)
          == std::vector<std::string>{"camera.position: expected (0.1, 0.2, "
                                      "0.3), got (0.1, 0.2, 0.3004)"});
    }
  }

  WHEN("the absolute tolerance covers the difference")
  {
    diff::DiffOptions options;
    options.tolerance.absolute = 1e-3;

    THEN("the captures match")
    {
      REQUIRE(compare(makeCapture(), actual, options).outcome
          == diff::Outcome::MATCH);
    }
  }

  WHEN("the absolute tolerance does not cover the difference")
  {
    diff::DiffOptions options;
    options.tolerance.absolute = 1e-5;

    THEN("they differ")
    {
      REQUIRE(compare(makeCapture(), actual, options).outcome
          == diff::Outcome::DIFFER);
    }
  }

  WHEN("the relative tolerance covers the difference")
  {
    diff::DiffOptions options;
    options.tolerance.relative = 2e-3;

    THEN("the captures match, and a smaller one does not")
    {
      REQUIRE(compare(makeCapture(), actual, options).outcome
          == diff::Outcome::MATCH);
      options.tolerance.relative = 1e-4;
      REQUIRE(compare(makeCapture(), actual, options).outcome
          == diff::Outcome::DIFFER);
    }
  }

  WHEN("neighboring floats are compared by their distance in ULPs")
  {
    // 1 and the next float up are 1 ULP apart; 1 and the one after are 2
    const auto one =
        makeCapture({{"fovy ANARI_FLOAT32 0.5", "fovy ANARI_FLOAT32 1"}});
    const auto oneUlp = makeCapture(
        {{"fovy ANARI_FLOAT32 0.5", "fovy ANARI_FLOAT32 1.0000001"}});
    const auto twoUlps = makeCapture(
        {{"fovy ANARI_FLOAT32 0.5", "fovy ANARI_FLOAT32 1.0000002"}});
    diff::DiffOptions options;
    options.tolerance.ulps = 1;

    THEN("the count of ULPs decides")
    {
      REQUIRE(compare(one, oneUlp, options).outcome == diff::Outcome::MATCH);
      REQUIRE(compare(one, twoUlps, options).outcome == diff::Outcome::DIFFER);
      REQUIRE(compare(one, oneUlp).outcome == diff::Outcome::DIFFER);
    }
  }

  WHEN("a tolerance is set but the values are integers")
  {
    diff::DiffOptions options;
    options.tolerance.absolute = 10;

    THEN("integers still match exactly")
    {
      REQUIRE(
          compare(makeCapture(), makeCapture({{"(2, 2)", "(2, 3)"}}), options)
              .outcome
          == diff::Outcome::DIFFER);
    }
  }
}

namespace {

// Floats as the raw bytes of a blob
std::string blobOf(const std::vector<float> &values)
{
  return std::string(
      (const char *)values.data(), values.size() * sizeof(float));
}

// A capture whose vertex array is a blob named 'name', of 'count' vectors
std::string withBlob(const std::string &name, int count = 2)
{
  return makeCapture(
      {{"  dimensions (2)\n  data [\n    (0, 0, 0)\n    (1, 0, 0)\n  ]\n",
          "  dimensions (" + std::to_string(count) + ")\n  count "
              + std::to_string(count)
              + "\n  min (0, 0, 0)\n  max (1, 0, 0)\n  blob " + name + "\n"}});
}

} // namespace

SCENARIO("blobs are compared by content", "[capture_diff]")
{
  std::vector<float> positions(3 * 20, 0.f);
  for (size_t i = 0; i < positions.size(); ++i)
    positions[i] = float(i);

  auto expected = inMemory(withBlob("aaaaaaaaaaaaaaaa", 20));
  const auto bytes = blobOf(positions);
  expected.blobs["aaaaaaaaaaaaaaaa"].assign(bytes.begin(), bytes.end());

  WHEN("the blobs have the same name")
  {
    auto actual = inMemory(withBlob("aaaaaaaaaaaaaaaa", 20));
    actual.blobs = expected.blobs;

    THEN("the captures match")
    {
      REQUIRE(diff::diffCaptures(expected, actual, {}).outcome
          == diff::Outcome::MATCH);
    }
  }

  WHEN("an element of the blob differs")
  {
    auto changed = positions;
    changed[17 * 3 + 2] = 99.f;
    auto actual = inMemory(withBlob("bbbbbbbbbbbbbbbb", 20));
    const auto changedBytes = blobOf(changed);
    actual.blobs["bbbbbbbbbbbbbbbb"].assign(
        changedBytes.begin(), changedBytes.end());

    THEN("it is reported by index, with the element's values")
    {
      const auto result = diff::diffCaptures(expected, actual, {});
      REQUIRE(lines(result)
          == std::vector<std::string>{"world.surface[0].geometry.vertex."
                                      "position[17]: expected (51, 52, 53), "
                                      "got (51, 52, 99)"});
    }

    THEN("a tolerance applies to the elements of a blob too")
    {
      diff::DiffOptions options;
      options.tolerance.absolute = 100;
      REQUIRE(diff::diffCaptures(expected, actual, options).outcome
          == diff::Outcome::MATCH);
    }

    THEN("a blob that is missing is an error, not a difference")
    {
      actual.blobs.clear();
      REQUIRE(diff::diffCaptures(expected, actual, {}).outcome
          == diff::Outcome::INVALID);
    }
  }

  WHEN("many elements differ")
  {
    auto changed = positions;
    for (auto &v : changed)
      v += 1.f;
    auto actual = inMemory(withBlob("cccccccccccccccc", 20));
    const auto changedBytes = blobOf(changed);
    actual.blobs["cccccccccccccccc"].assign(
        changedBytes.begin(), changedBytes.end());
    diff::DiffOptions options;
    options.maxIndicesPerArray = 3;
    const auto result = diff::diffCaptures(expected, actual, options);

    THEN("only the first few indices are reported, and all are counted")
    {
      REQUIRE(result.totalDifferences == 20);
      REQUIRE(result.differences.size() == 4);
      REQUIRE(result.differences[3].message()
          == "world.surface[0].geometry.vertex.position: 17 more elements "
             "differ");
    }
  }

  WHEN("one capture keeps the array inline and the other has a blob")
  {
    auto actual = inMemory(makeCapture());

    THEN("the elements are compared all the same")
    {
      const std::vector<float> two = {0, 0, 0, 1, 0, 0};
      auto blobbed = inMemory(withBlob("dddddddddddddddd"));
      const auto twoBytes = blobOf(two);
      blobbed.blobs["dddddddddddddddd"].assign(
          twoBytes.begin(), twoBytes.end());
      REQUIRE(diff::diffCaptures(actual, blobbed, {}).outcome
          == diff::Outcome::MATCH);
      const std::vector<float> other = {0, 0, 0, 1, 0, 5};
      auto differing = inMemory(withBlob("eeeeeeeeeeeeeeee"));
      const auto otherBytes = blobOf(other);
      differing.blobs["eeeeeeeeeeeeeeee"].assign(
          otherBytes.begin(), otherBytes.end());
      REQUIRE(lines(diff::diffCaptures(actual, differing, {}))
          == std::vector<std::string>{
              "world.surface[0].geometry.vertex."
              "position[1]: expected (1, 0, 0), got (1, "
              "0, 5)"});
    }
  }
}

SCENARIO("a capture is the same graph however its blocks are ordered",
    "[capture_diff]")
{
  // The same capture with the blocks of the camera and the world swapped
  std::string text = makeCapture();
  const auto cameraStart = text.find("camera@0 \"perspective\" {");
  const auto worldStart = text.find("world@0 {");
  const std::string camera = text.substr(cameraStart, worldStart - cameraStart);
  text.erase(cameraStart, camera.size());
  text += "\n" + camera.substr(0, camera.size() - 1);

  REQUIRE(text != makeCapture());
  REQUIRE(compare(makeCapture(), text).outcome == diff::Outcome::MATCH);
}

SCENARIO("objects are paired by their place in the graph, not by their IDs",
    "[capture_diff]")
{
  // Every ID of an object that is not the frame is renamed
  std::string text = makeCapture();
  for (const char *label :
      {"camera", "world", "surface", "geometry", "array1d"}) {
    for (int n = 1; n >= 0; --n) {
      const std::string from = std::string(label) + "@" + std::to_string(n);
      const std::string to = std::string(label) + "@" + std::to_string(n + 7);
      size_t at = 0;
      while ((at = text.find(from, at)) != std::string::npos) {
        text.replace(at, from.size(), to);
        at += to.size();
      }
    }
  }
  REQUIRE(text != makeCapture());
  REQUIRE(compare(makeCapture(), text).outcome == diff::Outcome::MATCH);
}

SCENARIO("object arrays can be compared as unordered", "[capture_diff]")
{
  // The surfaces in the other order: the array lists the sphere's first
  const auto swapped = makeCapture(
      {{"    surface@0\n    surface@1\n", "    surface@1\n    surface@0\n"}});

  WHEN("arrays are ordered")
  {
    const auto result = compare(makeCapture(), swapped);

    THEN("the order differs")
    {
      REQUIRE(result.outcome == diff::Outcome::DIFFER);
      REQUIRE(lines(result)[0].rfind("world.surface[0]", 0) == 0);
    }
  }

  WHEN("the array's path is named as unordered")
  {
    diff::DiffOptions options;
    options.unorderedPaths = {"world.surface"};

    THEN("the same surfaces in another order match")
    {
      REQUIRE(compare(makeCapture(), swapped, options).outcome
          == diff::Outcome::MATCH);
    }

    THEN("a surface that has no match is reported")
    {
      const auto different = makeCapture(
          {{"    surface@0\n    surface@1\n", "    surface@1\n    surface@0\n"},
              {"radius ANARI_FLOAT32 2", "radius ANARI_FLOAT32 3"}});
      const auto result = compare(makeCapture(), different, options);
      REQUIRE(result.outcome == diff::Outcome::DIFFER);
      REQUIRE(lines(result)
          == std::vector<std::string>{
              "world.surface[1]: no matching element, expected a surface",
              "world.surface[0]: unexpected element, a surface"});
    }

    THEN("floats within the tolerance still pair")
    {
      const auto nearby = makeCapture(
          {{"    surface@0\n    surface@1\n", "    surface@1\n    surface@0\n"},
              {"radius ANARI_FLOAT32 2", "radius ANARI_FLOAT32 2.0001"}});
      options.tolerance.absolute = 1e-3;
      REQUIRE(compare(makeCapture(), nearby, options).outcome
          == diff::Outcome::MATCH);
    }
  }

  WHEN("another array is named as unordered")
  {
    diff::DiffOptions options;
    options.unorderedPaths = {"world.instance"};

    THEN("this one stays ordered")
    {
      REQUIRE(compare(makeCapture(), swapped, options).outcome
          == diff::Outcome::DIFFER);
    }
  }
}

SCENARIO("the report is capped, and says how many differences there are",
    "[capture_diff]")
{
  const auto actual = makeCapture({{"(2, 2)", "(3, 3)"},
      {"fovy ANARI_FLOAT32 0.5", "fovy ANARI_FLOAT32 0.75"},
      {"radius ANARI_FLOAT32 2", "radius ANARI_FLOAT32 3"}});
  diff::DiffOptions options;
  options.maxDifferences = 2;
  const auto result = compare(makeCapture(), actual, options);

  REQUIRE(result.outcome == diff::Outcome::DIFFER);
  REQUIRE(result.totalDifferences == 3);
  REQUIRE(result.differences.size() == 2);
  const auto report = result.report();
  REQUIRE(report.find("1 more difference") != std::string::npos);
  REQUIRE(report.find("3 differences") != std::string::npos);
}

SCENARIO("a capture the diff cannot read is invalid, not different",
    "[capture_diff]")
{
  WHEN("the format version is not one it supports")
  {
    auto text = makeCapture();
    text.replace(0, std::strlen("anari-capture 1"), "anari-capture 2");
    const auto result = compare(makeCapture(), text);

    THEN("it says so")
    {
      REQUIRE(result.outcome == diff::Outcome::INVALID);
      REQUIRE(result.error.find("version 2") != std::string::npos);
    }
  }

  WHEN("the text is not a capture")
  {
    THEN("it is invalid")
    {
      REQUIRE(
          compare(makeCapture(), "hello\n").outcome == diff::Outcome::INVALID);
      REQUIRE(compare(makeCapture(), "").outcome == diff::Outcome::INVALID);
    }
  }
}

namespace {

namespace fs = std::filesystem;

struct TempDir
{
  fs::path path;

  TempDir()
  {
    static int counter = 0;
    path = fs::temp_directory_path()
        / ("anari_capture_diff_test_" + std::to_string(counter++) + "_"
            + std::to_string(std::rand()));
    fs::remove_all(path);
    fs::create_directories(path);
  }

  ~TempDir()
  {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

void writeFile(const fs::path &p, const std::string &contents)
{
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << contents;
}

// The exit code of the command-line tool
int runTool(const std::string &arguments)
{
  const std::string command =
      std::string("\"") + CAPTURE_DIFF_EXE + "\" --quiet " + arguments;
  const int status = std::system(command.c_str());
#ifdef _WIN32
  return status;
#else
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

} // namespace

SCENARIO(
    "captures are compared as files, with blobs next to them", "[capture_diff]")
{
  TempDir dir;
  const auto a = (dir.path / "a" / "x.capture").string();
  const auto b = (dir.path / "b" / "y.capture").string();

  WHEN("the files hold the same blobs under the same name")
  {
    writeFile(a, withBlob("aaaaaaaaaaaaaaaa"));
    writeFile(b, withBlob("aaaaaaaaaaaaaaaa"));
    const auto bytes = blobOf({0, 0, 0, 1, 0, 0});
    writeFile(dir.path / "a" / "blobs" / "aaaaaaaaaaaaaaaa.bin", bytes);
    writeFile(dir.path / "b" / "blobs" / "aaaaaaaaaaaaaaaa.bin", bytes);

    THEN("they match")
    {
      REQUIRE(diff::diffCaptureFiles(a, b, {}).outcome == diff::Outcome::MATCH);
    }
  }

  WHEN("the blobs differ")
  {
    writeFile(a, withBlob("aaaaaaaaaaaaaaaa"));
    writeFile(b, withBlob("bbbbbbbbbbbbbbbb"));
    writeFile(dir.path / "a" / "blobs" / "aaaaaaaaaaaaaaaa.bin",
        blobOf({0, 0, 0, 1, 0, 0}));
    writeFile(dir.path / "b" / "blobs" / "bbbbbbbbbbbbbbbb.bin",
        blobOf({0, 0, 0, 1, 0, 2}));

    THEN("the files differ at the element")
    {
      const auto result = diff::diffCaptureFiles(a, b, {});
      REQUIRE(lines(result)
          == std::vector<std::string>{
              "world.surface[0].geometry.vertex."
              "position[1]: expected (1, 0, 0), got (1, "
              "0, 2)"});
    }
  }

  WHEN("a file does not exist")
  {
    writeFile(a, makeCapture());

    THEN("the result is invalid")
    {
      REQUIRE(
          diff::diffCaptureFiles(a, b, {}).outcome == diff::Outcome::INVALID);
    }
  }
}

SCENARIO("the command-line tool exits 0, 1 or 2", "[capture_diff]")
{
  TempDir dir;
  const auto a = (dir.path / "a.capture").string();
  const auto b = (dir.path / "b.capture").string();
  const auto q = [](const std::string &p) { return "\"" + p + "\""; };

  writeFile(a, makeCapture());

  WHEN("the captures match")
  {
    writeFile(b, makeCapture());
    THEN("it exits 0")
    {
      REQUIRE(runTool(q(a) + " " + q(b)) == 0);
    }
  }

  WHEN("the captures differ")
  {
    writeFile(b, makeCapture({{"(2, 2)", "(2, 3)"}}));

    THEN("it exits 1")
    {
      REQUIRE(runTool(q(a) + " " + q(b)) == 1);
    }

    THEN("a tolerance can not make an integer match")
    {
      REQUIRE(runTool("--abs-tol 1 " + q(a) + " " + q(b)) == 1);
    }
  }

  WHEN("the captures differ by a float")
  {
    writeFile(b, makeCapture({{"(0.1, 0.2, 0.3)", "(0.1, 0.2, 0.3004)"}}));

    THEN("options set the tolerance and the unordered paths")
    {
      REQUIRE(runTool(q(a) + " " + q(b)) == 1);
      REQUIRE(runTool("--abs-tol 0.001 " + q(a) + " " + q(b)) == 0);
      REQUIRE(runTool("--rel-tol 0.01 " + q(a) + " " + q(b)) == 0);
      REQUIRE(runTool("--ulps 100000 " + q(a) + " " + q(b)) == 0);
      REQUIRE(runTool("--max-differences 1 " + q(a) + " " + q(b)) == 1);
      REQUIRE(runTool("--unordered world.surface " + q(a) + " " + q(b)) == 1);
    }
  }

  WHEN("an input is unreadable")
  {
    THEN("it exits 2")
    {
      REQUIRE(runTool(q(a) + " " + q((dir.path / "missing").string())) == 2);
    }
  }

  WHEN("the format version is not supported")
  {
    auto text = makeCapture();
    text.replace(0, std::strlen("anari-capture 1"), "anari-capture 9");
    writeFile(b, text);

    THEN("it exits 2")
    {
      REQUIRE(runTool(q(a) + " " + q(b)) == 2);
    }
  }

  WHEN("it is run wrongly")
  {
    THEN("it exits 2")
    {
      REQUIRE(runTool(q(a)) == 2);
      REQUIRE(runTool("--no-such-option " + q(a) + " " + q(a)) == 2);
    }
  }
}
