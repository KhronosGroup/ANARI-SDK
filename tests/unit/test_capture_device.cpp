// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Tests of the 'capture' device through the public ANARI API only: build a
// small scene, render, and check what comes out (the capture text, blobs,
// mapped frame contents, query answers, status messages).

#include "catch.hpp"

#include <anari/anari_cpp/ext/linalg.h>
#include <anari/ext/capture/CaptureDiff.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

// Status messages collected from the device ///////////////////////////////////

struct Status
{
  std::vector<std::string> warnings;
  std::vector<std::string> errors;
};

void collectStatus(const void *userPtr,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  auto *s = (Status *)userPtr;
  if (severity == ANARI_SEVERITY_WARNING)
    s->warnings.push_back(message);
  else if (severity == ANARI_SEVERITY_ERROR
      || severity == ANARI_SEVERITY_FATAL_ERROR)
    s->errors.push_back(message);
}

// A loaded capture device ////////////////////////////////////////////////////

struct CaptureDevice
{
  Status status;
  anari::Library lib{nullptr};
  anari::Device device{nullptr};

  CaptureDevice()
  {
    lib = anari::loadLibrary("capture", collectStatus, &status);
    REQUIRE(lib != nullptr);
    device = anari::newDevice(lib, "default");
    REQUIRE(device != nullptr);
  }

  ~CaptureDevice()
  {
    anari::release(device, device);
    anari::unloadLibrary(lib);
  }
};

// Reads a string property: '<name>.size' gives the size (with NUL), then
// '<name>' copies the string.
std::string getStringProperty(
    anari::Device d, anari::Object o, const char *name)
{
  uint64_t size = 0;
  const std::string sizeName = std::string(name) + ".size";
  if (!anariGetProperty(d,
          o,
          sizeName.c_str(),
          ANARI_UINT64,
          &size,
          sizeof(size),
          ANARI_NO_WAIT)
      || size == 0)
    return {};
  std::vector<char> buf(size, 0);
  if (!anariGetProperty(
          d, o, name, ANARI_STRING, buf.data(), buf.size(), ANARI_NO_WAIT))
    return {};
  return std::string(buf.data());
}

std::string captureOf(anari::Device d, anari::Frame f)
{
  return getStringProperty(d, f, "capture");
}

// The capture without its header line and device block (which lists the long
// default extension list), so a test can state the rest of it exactly.
std::string body(const std::string &capture)
{
  const std::string header = "anari-capture 1\n\n";
  REQUIRE(capture.rfind(header, 0) == 0);
  const std::string deviceEnd = "\n}\n\n";
  const auto end = capture.find(deviceEnd);
  REQUIRE(end != std::string::npos);
  return capture.substr(end + deviceEnd.size());
}

// A frame with a camera, a renderer and a world, committed and ready to render.
struct Scene
{
  anari::Device d;
  anari::Frame frame;
  anari::World world;
  anari::Camera camera;
  anari::Renderer renderer;

  explicit Scene(anari::Device dev) : d(dev)
  {
    frame = anari::newObject<anari::Frame>(d);
    world = anari::newObject<anari::World>(d);
    camera = anari::newObject<anari::Camera>(d, "perspective");
    renderer = anari::newObject<anari::Renderer>(d, "default");
    anari::commitParameters(d, world);
    anari::commitParameters(d, camera);
    anari::commitParameters(d, renderer);
    anari::setParameter(d, frame, "world", world);
    anari::setParameter(d, frame, "camera", camera);
    anari::setParameter(d, frame, "renderer", renderer);
    anari::setParameter(d, frame, "size", anari::math::uint2(2, 2));
    anari::commitParameters(d, frame);
  }

  ~Scene()
  {
    anari::release(d, frame);
    anari::release(d, world);
    anari::release(d, camera);
    anari::release(d, renderer);
  }

  void commit(anari::Object o)
  {
    anari::commitParameters(d, o);
  }

  std::string render()
  {
    anari::render(d, frame);
    anari::wait(d, frame);
    return captureOf(d, frame);
  }
};

} // namespace

SCENARIO("a capture starts with a versioned header", "[capture_device]")
{
  CaptureDevice cd;
  Scene scene(cd.device);

  WHEN("a frame is rendered")
  {
    const std::string capture = scene.render();

    THEN("the first line is the format header")
    {
      REQUIRE(capture.rfind("anari-capture 1\n", 0) == 0);
    }
  }
}

namespace {

// The fixture scene, as it is captured.
const char *kEmptyScene =
    "frame@0 {\n"
    "  camera ANARI_CAMERA camera@0\n"
    "  renderer ANARI_RENDERER renderer@0\n"
    "  size ANARI_UINT32_VEC2 (2, 2)\n"
    "  world ANARI_WORLD world@0\n"
    "}\n"
    "\n"
    "camera@0 \"perspective\" {\n"
    "}\n"
    "\n"
    "renderer@0 \"default\" {\n"
    "}\n"
    "\n"
    "world@0 {\n"
    "}\n";

// Surfaces over two geometries sharing one material, made in the given order,
// in an instance of a group in the scene's world. Returns the handles to
// release.
struct Content
{
  anari::Device d;
  std::vector<anari::Object> owned;

  template <typename T>
  T keep(T o)
  {
    owned.push_back((anari::Object)o);
    return o;
  }

  ~Content()
  {
    for (auto o : owned)
      anari::release(d, o);
  }
};

void buildSharedMaterialScene(
    Scene &scene, Content &c, bool geometryCylinderFirst)
{
  auto d = scene.d;
  c.d = d;
  anari::Geometry sphere{}, cylinder{};
  if (geometryCylinderFirst) {
    cylinder = c.keep(anari::newObject<anari::Geometry>(d, "cylinder"));
    sphere = c.keep(anari::newObject<anari::Geometry>(d, "sphere"));
  } else {
    sphere = c.keep(anari::newObject<anari::Geometry>(d, "sphere"));
    cylinder = c.keep(anari::newObject<anari::Geometry>(d, "cylinder"));
  }
  auto material = c.keep(anari::newObject<anari::Material>(d, "matte"));
  auto s0 = c.keep(anari::newObject<anari::Surface>(d));
  auto s1 = c.keep(anari::newObject<anari::Surface>(d));
  anari::setParameter(d, s0, "geometry", sphere);
  anari::setParameter(d, s0, "material", material);
  anari::setParameter(d, s1, "geometry", cylinder);
  anari::setParameter(d, s1, "material", material);
  for (auto o : {(anari::Object)sphere,
           (anari::Object)cylinder,
           (anari::Object)material,
           (anari::Object)s0,
           (anari::Object)s1})
    anari::commitParameters(d, o);

  anari::Surface surfaces[] = {s0, s1};
  auto surfaceArray = c.keep(anari::newArray1D(d, surfaces, 2));
  auto group = c.keep(anari::newObject<anari::Group>(d));
  anari::setParameter(d, group, "surface", surfaceArray);
  anari::commitParameters(d, group);

  auto instance = c.keep(anari::newObject<anari::Instance>(d, "transform"));
  anari::setParameter(d, instance, "group", group);
  anari::commitParameters(d, instance);

  anari::Instance instances[] = {instance};
  auto instanceArray = c.keep(anari::newArray1D(d, instances, 1));
  anari::setParameter(d, scene.world, "instance", instanceArray);
  anari::commitParameters(d, scene.world);
}

} // namespace

SCENARIO(
    "a capture is the committed graph in canonical text", "[capture_device]")
{
  CaptureDevice cd;
  Scene scene(cd.device);

  WHEN("the frame has only its camera, renderer and world")
  {
    THEN("each is a block labeled by a typed ID")
    {
      REQUIRE(body(scene.render()) == kEmptyScene);
      REQUIRE(cd.status.warnings.empty());
    }
  }

  WHEN("a parameter is set and committed")
  {
    anari::setParameter(cd.device, scene.camera, "fovy", 0.5f);
    scene.commit(scene.camera);

    THEN("it is written with its ANARI type and value")
    {
      REQUIRE(body(scene.render())
                  .find("camera@0 \"perspective\" {\n"
                        "  fovy ANARI_FLOAT32 0.5\n"
                        "}\n")
          != std::string::npos);
    }
  }

  WHEN("a parameter is set but never committed")
  {
    anari::setParameter(cd.device, scene.camera, "fovy", 0.5f);

    THEN("it is absent")
    {
      REQUIRE(body(scene.render()) == kEmptyScene);
    }
  }

  WHEN("a committed parameter is set to another value without a commit")
  {
    anari::setParameter(cd.device, scene.camera, "fovy", 0.5f);
    scene.commit(scene.camera);
    anari::setParameter(cd.device, scene.camera, "fovy", 1.5f);

    THEN("the committed value is captured")
    {
      REQUIRE(
          scene.render().find("fovy ANARI_FLOAT32 0.5\n") != std::string::npos);
    }
  }

  WHEN("a committed parameter is unset and committed")
  {
    anari::setParameter(cd.device, scene.camera, "fovy", 0.5f);
    scene.commit(scene.camera);
    anari::unsetParameter(cd.device, scene.camera, "fovy");
    scene.commit(scene.camera);

    THEN("it is absent")
    {
      REQUIRE(body(scene.render()) == kEmptyScene);
    }
  }

  WHEN("a committed parameter is unset but not committed")
  {
    anari::setParameter(cd.device, scene.camera, "fovy", 0.5f);
    scene.commit(scene.camera);
    anari::unsetParameter(cd.device, scene.camera, "fovy");

    THEN("the committed value is still captured")
    {
      REQUIRE(
          scene.render().find("fovy ANARI_FLOAT32 0.5\n") != std::string::npos);
    }
  }

  WHEN("an object the frame cannot reach exists")
  {
    auto stray = anari::newObject<anari::Camera>(cd.device, "orthographic");
    anari::commitParameters(cd.device, stray);

    THEN("it is left out")
    {
      REQUIRE(body(scene.render()) == kEmptyScene);
    }
    anari::release(cd.device, stray);
  }

  WHEN("scalar, vector, boolean and string parameters are committed")
  {
    anari::setParameter(cd.device, scene.world, "name", "my \"world\"\n");
    anari::setParameter(cd.device,
        scene.camera,
        "position",
        anari::math::float3(0.1f, 0.2f, 0.3f));
    anari::setParameter(cd.device, scene.camera, "aspect", 1.0f / 3.0f);
    anari::setParameter(cd.device, scene.renderer, "ambientRadiance", 0.0f);
    scene.commit(scene.world);
    scene.commit(scene.camera);
    scene.commit(scene.renderer);
    const auto text = body(scene.render());

    THEN("each is written typed, with floats that read back exactly")
    {
      REQUIRE(text.find("name ANARI_STRING \"my \\\"world\\\"\\n\"\n")
          != std::string::npos);
      REQUIRE(text.find("position ANARI_FLOAT32_VEC3 (0.1, 0.2, 0.3)\n")
          != std::string::npos);
      REQUIRE(
          text.find("aspect ANARI_FLOAT32 0.33333334\n") != std::string::npos);
      REQUIRE(
          text.find("ambientRadiance ANARI_FLOAT32 0\n") != std::string::npos);
    }
  }
}

SCENARIO(
    "a capture names objects by their place in the graph", "[capture_device]")
{
  CaptureDevice cd;
  Scene scene(cd.device);
  Content content;
  buildSharedMaterialScene(scene, content, false);

  WHEN("two surfaces share a material")
  {
    const auto text = body(scene.render());

    THEN("the shared object is written once and referenced by ID")
    {
      REQUIRE(cd.status.warnings.empty());
      REQUIRE(text
          == std::string(kEmptyScene)
                 .replace(std::string(kEmptyScene).find("world@0 {\n}\n"),
                     std::string("world@0 {\n}\n").size(),
                     "world@0 {\n"
                     "  instance ANARI_ARRAY1D array1d@0\n"
                     "}\n"
                     "\n"
                     "array1d@0 {\n"
                     "  elementType ANARI_INSTANCE\n"
                     "  dimensions (1)\n"
                     "  data [\n"
                     "    instance@0\n"
                     "  ]\n"
                     "}\n"
                     "\n"
                     "instance@0 \"transform\" {\n"
                     "  group ANARI_GROUP group@0\n"
                     "}\n"
                     "\n"
                     "group@0 {\n"
                     "  surface ANARI_ARRAY1D array1d@1\n"
                     "}\n"
                     "\n"
                     "array1d@1 {\n"
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
                     "  material ANARI_MATERIAL material@0\n"
                     "}\n"
                     "\n"
                     "geometry@0 \"sphere\" {\n"
                     "}\n"
                     "\n"
                     "material@0 \"matte\" {\n"
                     "}\n"
                     "\n"
                     "surface@1 {\n"
                     "  geometry ANARI_GEOMETRY geometry@1\n"
                     "  material ANARI_MATERIAL material@0\n"
                     "}\n"
                     "\n"
                     "geometry@1 \"cylinder\" {\n"
                     "}\n"));
    }
  }
}

SCENARIO("a capture does not depend on the order objects were created in",
    "[capture_device]")
{
  CaptureDevice cd;
  std::string first, second;
  {
    Scene scene(cd.device);
    Content content;
    buildSharedMaterialScene(scene, content, false);
    first = scene.render();
  }
  {
    Scene scene(cd.device);
    Content content;
    buildSharedMaterialScene(scene, content, true);
    second = scene.render();
  }

  THEN("the captures are identical")
  {
    REQUIRE(!first.empty());
    REQUIRE(first == second);
  }
}

SCENARIO("small arrays are written inline", "[capture_device]")
{
  CaptureDevice cd;
  Scene scene(cd.device);
  auto d = cd.device;

  WHEN("a 1D array of vectors is referenced by an object")
  {
    anari::math::float3 positions[] = {{0, 0, 0}, {1, 0, 0}, {0.5f, 1, 0}};
    auto array = anari::newArray1D(d, positions, 3);
    auto geometry = anari::newObject<anari::Geometry>(d, "triangle");
    anari::setParameter(d, geometry, "vertex.position", array);
    anari::commitParameters(d, geometry);
    auto surface = anari::newObject<anari::Surface>(d);
    anari::setParameter(d, surface, "geometry", geometry);
    anari::commitParameters(d, surface);
    anari::Surface surfaces[] = {surface};
    auto surfaceArray = anari::newArray1D(d, surfaces, 1);
    auto group = anari::newObject<anari::Group>(d);
    anari::setParameter(d, group, "surface", surfaceArray);
    anari::commitParameters(d, group);
    auto instance = anari::newObject<anari::Instance>(d, "transform");
    anari::setParameter(d, instance, "group", group);
    anari::commitParameters(d, instance);
    anari::Instance instances[] = {instance};
    auto instanceArray = anari::newArray1D(d, instances, 1);
    anari::setParameter(d, scene.world, "instance", instanceArray);
    anari::commitParameters(d, scene.world);
    const auto text = body(scene.render());

    THEN("its type, size and elements are listed")
    {
      REQUIRE(text.find("array1d@2 {\n"
                        "  elementType ANARI_FLOAT32_VEC3\n"
                        "  dimensions (3)\n"
                        "  data [\n"
                        "    (0, 0, 0)\n"
                        "    (1, 0, 0)\n"
                        "    (0.5, 1, 0)\n"
                        "  ]\n"
                        "}\n")
          != std::string::npos);
    }

    anari::release(d, array);
    anari::release(d, geometry);
    anari::release(d, surface);
    anari::release(d, surfaceArray);
    anari::release(d, group);
    anari::release(d, instance);
    anari::release(d, instanceArray);
  }

  WHEN("a 2D array is referenced")
  {
    uint8_t texels[] = {1, 2, 3, 4, 5, 6};
    auto array = anari::newArray2D(d, texels, 3, 2);
    auto sampler = anari::newObject<anari::Sampler>(d, "image2D");
    anari::setParameter(d, sampler, "image", array);
    anari::commitParameters(d, sampler);
    auto material = anari::newObject<anari::Material>(d, "matte");
    anari::setParameter(d, material, "color", sampler);
    anari::commitParameters(d, material);
    auto surface = anari::newObject<anari::Surface>(d);
    anari::setParameter(d, surface, "material", material);
    anari::commitParameters(d, surface);
    anari::Surface surfaces[] = {surface};
    auto surfaceArray = anari::newArray1D(d, surfaces, 1);
    auto group = anari::newObject<anari::Group>(d);
    anari::setParameter(d, group, "surface", surfaceArray);
    anari::commitParameters(d, group);
    auto instance = anari::newObject<anari::Instance>(d, "transform");
    anari::setParameter(d, instance, "group", group);
    anari::commitParameters(d, instance);
    anari::Instance instances[] = {instance};
    auto instanceArray = anari::newArray1D(d, instances, 1);
    anari::setParameter(d, scene.world, "instance", instanceArray);
    anari::commitParameters(d, scene.world);
    const auto text = body(scene.render());

    THEN("its dimensions are listed in order")
    {
      REQUIRE(text.find("array2d@0 {\n"
                        "  elementType ANARI_UINT8\n"
                        "  dimensions (3, 2)\n"
                        "  data [\n"
                        "    1\n"
                        "    2\n"
                        "    3\n"
                        "    4\n"
                        "    5\n"
                        "    6\n"
                        "  ]\n"
                        "}\n")
          != std::string::npos);
    }

    anari::release(d, array);
    anari::release(d, sampler);
    anari::release(d, material);
    anari::release(d, surface);
    anari::release(d, surfaceArray);
    anari::release(d, group);
    anari::release(d, instance);
    anari::release(d, instanceArray);
  }
}

namespace {

// Reads the raw bytes of a blob the frame's last capture refers to
std::string getBlob(anari::Device d, anari::Frame f, const std::string &name)
{
  const std::string property = "capture.blob." + name;
  uint64_t size = 0;
  if (!anariGetProperty(d,
          f,
          (property + ".size").c_str(),
          ANARI_UINT64,
          &size,
          sizeof(size),
          ANARI_NO_WAIT))
    return {};
  std::string bytes(size, '\0');
  anariGetProperty(
      d, f, property.c_str(), ANARI_STRING, &bytes[0], size, ANARI_NO_WAIT);
  return bytes;
}

// The names of the blobs the frame's last capture refers to
std::vector<std::string> blobNames(anari::Device d, anari::Frame f)
{
  std::vector<std::string> names;
  const std::string all = getStringProperty(d, f, "capture.blobs");
  size_t at = 0;
  while (at < all.size()) {
    auto end = all.find('\n', at);
    if (end == std::string::npos)
      end = all.size();
    names.push_back(all.substr(at, end - at));
    at = end + 1;
  }
  return names;
}

// An array of its own holding a copy of 'n' elements, so the array does not
// depend on how long the app's memory lives
template <typename T>
anari::Array1D copyArray1D(anari::Device d, const T *data, size_t n)
{
  auto array = anari::newArray1D(d, anari::ANARITypeFor<T>::value, n);
  std::memcpy(anari::map<T>(d, array), data, n * sizeof(T));
  anari::unmap(d, array);
  return array;
}

template <typename T>
anari::Array2D copyArray2D(anari::Device d, const T *data, size_t w, size_t h)
{
  auto array = anari::newArray2D(d, anari::ANARITypeFor<T>::value, w, h);
  std::memcpy(anari::map<T>(d, array), data, w * h * sizeof(T));
  anari::unmap(d, array);
  return array;
}

// The array 'array' as the 'name' parameter of a geometry in a surface in a
// group in an instance in the scene's world. Its ID is then array1d@2 (after
// the instance and surface arrays), if it is a 1D array.
template <typename ArrayT>
void attachArray(Scene &scene, Content &c, const char *name, ArrayT array)
{
  auto d = scene.d;
  c.d = d;
  auto geometry = c.keep(anari::newObject<anari::Geometry>(d, "triangle"));
  anari::setParameter(d, geometry, name, array);
  anari::commitParameters(d, geometry);
  auto surface = c.keep(anari::newObject<anari::Surface>(d));
  anari::setParameter(d, surface, "geometry", geometry);
  anari::commitParameters(d, surface);
  anari::Surface surfaces[] = {surface};
  auto surfaceArray = c.keep(anari::newArray1D(d, surfaces, 1));
  auto group = c.keep(anari::newObject<anari::Group>(d));
  anari::setParameter(d, group, "surface", surfaceArray);
  anari::commitParameters(d, group);
  auto instance = c.keep(anari::newObject<anari::Instance>(d, "transform"));
  anari::setParameter(d, instance, "group", group);
  anari::commitParameters(d, instance);
  anari::Instance instances[] = {instance};
  auto instanceArray = c.keep(anari::newArray1D(d, instances, 1));
  anari::setParameter(d, scene.world, "instance", instanceArray);
  anari::commitParameters(d, scene.world);
}

// Any object as the 'name' parameter of a surface in the scene's world, through
// the same chain as attachArray()
template <typename ObjectT>
void attachObject(Scene &scene, Content &c, const char *name, ObjectT object)
{
  auto d = scene.d;
  c.d = d;
  auto surface = c.keep(anari::newObject<anari::Surface>(d));
  anari::setParameter(d, surface, name, object);
  anari::commitParameters(d, surface);
  anari::Surface surfaces[] = {surface};
  auto surfaceArray = c.keep(anari::newArray1D(d, surfaces, 1));
  auto group = c.keep(anari::newObject<anari::Group>(d));
  anari::setParameter(d, group, "surface", surfaceArray);
  anari::commitParameters(d, group);
  auto instance = c.keep(anari::newObject<anari::Instance>(d, "transform"));
  anari::setParameter(d, instance, "group", group);
  anari::commitParameters(d, instance);
  anari::Instance instances[] = {instance};
  auto instanceArray = c.keep(anari::newArray1D(d, instances, 1));
  anari::setParameter(d, scene.world, "instance", instanceArray);
  anari::commitParameters(d, scene.world);
}

// A string as it is written in a capture: quoted, with the quotes escaped
std::string quoteForTest(const std::string &s)
{
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\')
      out += '\\';
    out += c;
  }
  return out + "\"";
}

// The text of the block labeled 'id', up to and including its closing brace
std::string blockOf(const std::string &capture, const std::string &id)
{
  const auto start = capture.find("\n" + id + " {\n");
  if (start == std::string::npos)
    return {};
  const auto end = capture.find("\n}\n", start);
  return capture.substr(start + 1, end - start + 2);
}

} // namespace

SCENARIO("large arrays are written as blobs with a summary", "[capture_device]")
{
  CaptureDevice cd;
  Scene scene(cd.device);
  Content content;
  auto d = cd.device;

  GIVEN("an array of vectors with more elements than the default limit")
  {
    std::vector<anari::math::float3> positions;
    for (int i = 0; i < 65; ++i)
      positions.push_back({float(i), float(2 * i) - 10.f, 0.5f});
    auto array = content.keep(copyArray1D(d, positions.data(), 65));
    attachArray(scene, content, "vertex.position", array);
    const auto text = scene.render();
    const auto block = blockOf(text, "array1d@2");

    THEN("its elements are summarized, and its bytes are in a blob")
    {
      REQUIRE(block.find("array1d@2 {\n"
                         "  elementType ANARI_FLOAT32_VEC3\n"
                         "  dimensions (65)\n"
                         "  count 65\n"
                         "  min (0, -10, 0.5)\n"
                         "  max (64, 118, 0.5)\n"
                         "  blob ")
          == 0);
      REQUIRE(block.find("data [") == std::string::npos);

      const auto name = block.substr(block.find("blob ") + 5, 16);
      REQUIRE(blobNames(d, scene.frame) == std::vector<std::string>{name});
      const auto bytes = getBlob(d, scene.frame, name);
      REQUIRE(bytes.size() == positions.size() * sizeof(positions[0]));
      REQUIRE(std::memcmp(bytes.data(), positions.data(), bytes.size()) == 0);
    }
  }

  GIVEN("an array with exactly as many elements as the default limit")
  {
    std::vector<uint32_t> values(64, 7u);
    auto array = content.keep(copyArray1D(d, values.data(), 64));
    attachArray(scene, content, "primitive.index", array);

    THEN("it is still written inline")
    {
      const auto block = blockOf(scene.render(), "array1d@2");
      REQUIRE(block.find("data [") != std::string::npos);
      REQUIRE(blobNames(d, scene.frame).empty());
    }
  }

  GIVEN("a smaller inline limit set on the device")
  {
    anari::setParameter(d, d, "inlineArrayLimit", uint32_t(2));
    anari::commitParameters(d, d);
    uint32_t values[] = {1, 2, 3};
    auto array = content.keep(copyArray1D(d, values, 3));
    attachArray(scene, content, "primitive.index", array);

    THEN("arrays over that limit are blobs")
    {
      const auto block = blockOf(scene.render(), "array1d@2");
      REQUIRE(block.find("blob ") != std::string::npos);
    }
  }

  GIVEN("integer data and an inline limit of zero")
  {
    anari::setParameter(d, d, "inlineArrayLimit", uint32_t(0));
    anari::commitParameters(d, d);
    uint8_t values[] = {1, 2, 3, 4};
    auto array = content.keep(copyArray1D(d, values, 4));
    attachArray(scene, content, "primitive.index", array);

    THEN("the blob is named by a hash of its type, size and bytes")
    {
      const auto block = blockOf(scene.render(), "array1d@2");
      REQUIRE(block
          == "array1d@2 {\n"
             "  elementType ANARI_UINT8\n"
             "  dimensions (4)\n"
             "  count 4\n"
             "  min 1\n"
             "  max 4\n"
             "  blob f05edf276f4dba45\n"
             "}\n");
    }

    THEN("another type, shape or content has another name")
    {
      uint8_t same[] = {1, 2, 3, 4};
      uint8_t other[] = {1, 2, 3, 5};
      auto a = content.keep(copyArray1D(d, same, 4));
      auto b = content.keep(copyArray2D(d, same, 2, 2));
      auto c = content.keep(copyArray1D(d, other, 4));
      std::vector<std::string> names;
      auto attach = [&](Scene &s, Content &content, int which) {
        if (which == 0)
          attachArray(s, content, "primitive.index", a);
        else if (which == 1)
          attachArray(s, content, "primitive.index", b);
        else
          attachArray(s, content, "primitive.index", c);
      };
      for (int which = 0; which < 3; ++which) {
        Scene s(d);
        Content content;
        attach(s, content, which);
        const auto block = blockOf(s.render(), "array1d@2");
        if (block.empty()) {
          const auto block2d = blockOf(s.render(), "array2d@0");
          names.push_back(block2d.substr(block2d.find("blob ") + 5, 16));
        } else {
          names.push_back(block.substr(block.find("blob ") + 5, 16));
        }
      }
      REQUIRE(names[0] == "f05edf276f4dba45");
      REQUIRE(names[1] != names[0]);
      REQUIRE(names[2] != names[0]);
    }
  }
}

SCENARIO("the summary of a blob ignores NaNs unless every value is one",
    "[capture_device]")
{
  CaptureDevice cd;
  Scene scene(cd.device);
  Content content;
  auto d = cd.device;

  GIVEN("an array of floats with a NaN first, one in the middle and at the end")
  {
    std::vector<float> values(70, 1.f);
    values[0] = std::nanf("");
    values[5] = -3.f;
    values[6] = 9.f;
    values[30] = std::nanf("");
    values[69] = std::nanf("");
    auto array = content.keep(copyArray1D(d, values.data(), 70));
    attachArray(scene, content, "primitive.attribute0", array);
    const auto block = blockOf(scene.render(), "array1d@2");

    THEN("the smallest and largest values are numbers")
    {
      REQUIRE(block.find("  min -3\n  max 9\n") != std::string::npos);
    }
  }

  GIVEN("an array in which every value is a NaN")
  {
    std::vector<float> values(70, std::nanf(""));
    auto array = content.keep(copyArray1D(d, values.data(), 70));
    attachArray(scene, content, "primitive.attribute0", array);
    const auto block = blockOf(scene.render(), "array1d@2");

    THEN("so are the smallest and largest")
    {
      REQUIRE(block.find("  min nan\n  max nan\n") != std::string::npos);
    }
  }
}

SCENARIO("a capture holds what the app's arrays contain at render time",
    "[capture_device]")
{
  CaptureDevice cd;
  Scene scene(cd.device);
  Content content;
  auto d = cd.device;

  GIVEN("an array that shares the app's memory")
  {
    float values[] = {1.f, 2.f, 3.f};
    auto array = anari::newArray1D(d, values, 3);
    anari::commitParameters(d, array);
    attachArray(scene, content, "primitive.attribute0", array);

    WHEN("the app changes the memory before rendering")
    {
      values[1] = 5.f;

      THEN("the new contents are captured")
      {
        const auto block = blockOf(scene.render(), "array1d@2");
        REQUIRE(block.find("    1\n    5\n    3\n") != std::string::npos);
      }
    }

    // While the memory is still there: the device copies it on this release
    anari::release(d, array);
  }

  GIVEN("an array whose elements are floats of every difficulty")
  {
    const float values[] = {0.1f,
        1.f / 3.f,
        16777216.f,
        1e-30f,
        -0.f,
        3.4028235e38f,
        1.17549435e-38f,
        1e-45f};
    auto array = content.keep(copyArray1D(d, values, 8));
    attachArray(scene, content, "primitive.attribute0", array);
    const auto block = blockOf(scene.render(), "array1d@2");

    THEN("each reads back to the same bits")
    {
      size_t at = block.find("data [\n") + 7;
      for (float v : values) {
        at += 4; // indent
        const auto end = block.find('\n', at);
        const float back =
            std::strtof(block.substr(at, end - at).c_str(), nullptr);
        uint32_t a, b;
        std::memcpy(&a, &v, 4);
        std::memcpy(&b, &back, 4);
        REQUIRE(a == b);
        at = end + 1;
      }
    }
  }

  GIVEN("an array of doubles")
  {
    const double values[] = {0.1, 1.0 / 3.0, 1e300, -2.5e-300};
    auto array = content.keep(copyArray1D(d, values, 4));
    attachArray(scene, content, "primitive.attribute0", array);
    const auto block = blockOf(scene.render(), "array1d@2");

    THEN("each reads back to the same bits")
    {
      size_t at = block.find("data [\n") + 7;
      for (double v : values) {
        at += 4; // indent
        const auto end = block.find('\n', at);
        const double back =
            std::strtod(block.substr(at, end - at).c_str(), nullptr);
        REQUIRE(back == v);
        at = end + 1;
      }
    }
  }
}

namespace {
int g_callbackCalls = 0;
void countCallback(const void *, ANARIDevice, ANARIFrame)
{
  ++g_callbackCalls;
}
} // namespace

SCENARIO(
    "pointers and callbacks are written as placeholders", "[capture_device]")
{
  CaptureDevice cd;
  Scene scene(cd.device);
  auto d = cd.device;

  int a = 0, b = 0;
  anari::setParameter(d, scene.world, "userData", (const void *)&a);
  scene.commit(scene.world);
  const auto first = scene.render();

  THEN("the type is kept and the value is not")
  {
    REQUIRE(first.find("  userData ANARI_VOID_POINTER <opaque>\n")
        != std::string::npos);
  }

  THEN("two runs with other pointers write the same capture")
  {
    anari::setParameter(d, scene.world, "userData", (const void *)&b);
    scene.commit(scene.world);
    REQUIRE(scene.render() == first);
  }

  WHEN("the frame has a completion callback")
  {
    g_callbackCalls = 0;
    anari::setParameter(d,
        scene.frame,
        "frameCompletionCallback",
        (ANARIFrameCompletionCallback)countCallback);
    scene.commit(scene.frame);
    const auto text = scene.render();

    THEN("it is called once per render, and written as a placeholder")
    {
      REQUIRE(g_callbackCalls == 1);
      REQUIRE(text.find("  frameCompletionCallback "
                        "ANARI_FRAME_COMPLETION_CALLBACK <opaque>\n")
          != std::string::npos);
    }
  }
}

namespace {

std::vector<std::string> extensionsOf(anari::Device d)
{
  const char *const *list = nullptr;
  REQUIRE(anariGetProperty(d,
              d,
              "extension",
              ANARI_STRING_LIST,
              &list,
              sizeof(list),
              ANARI_NO_WAIT)
      == 1);
  std::vector<std::string> out;
  for (; list && *list; ++list)
    out.push_back(*list);
  return out;
}

bool has(const std::vector<std::string> &list, const std::string &s)
{
  return std::find(list.begin(), list.end(), s) != list.end();
}

} // namespace

SCENARIO("the device advertises every extension unless told otherwise",
    "[capture_device]")
{
  CaptureDevice cd;
  auto d = cd.device;

  WHEN("no extensions are set")
  {
    const auto extensions = extensionsOf(d);

    THEN(
        "it advertises everything the spec defines, including what no real "
        "device here implements")
    {
      REQUIRE(has(extensions, "ANARI_KHR_GEOMETRY_TRIANGLE"));
      REQUIRE(has(extensions, "ANARI_KHR_LIGHT_HDRI"));
      REQUIRE(has(extensions, "ANARI_KHR_SPATIAL_FIELD_UNSTRUCTURED"));
      REQUIRE(has(extensions, "ANARI_KHR_DATA_PARALLEL_MPI"));
    }

    THEN("the library reports the same list")
    {
      std::vector<std::string> fromLibrary;
      for (auto **e = anariGetDeviceExtensions(cd.lib, "default"); *e; ++e)
        fromLibrary.push_back(*e);
      auto sorted = fromLibrary;
      std::sort(sorted.begin(), sorted.end());
      REQUIRE(sorted == extensions);
    }

    THEN("the capture lists them, sorted")
    {
      Scene scene(d);
      const auto capture = scene.render();
      REQUIRE(capture.find("device {\n"
                           "  extensions ANARI_STRING_LIST [\n"
                           "    \"ANARI_KHR_ARRAY1D_REGION\"\n")
          != std::string::npos);
      REQUIRE(
          capture.find("    \"ANARI_KHR_LIGHT_HDRI\"\n") != std::string::npos);
    }
  }

  WHEN("a list of extensions is set on the device")
  {
    const char *list[] = {
        "ANARI_KHR_GEOMETRY_TRIANGLE", "ANARI_KHR_CAMERA_PERSPECTIVE", nullptr};
    anariSetParameter(d, d, "extensions", ANARI_STRING_LIST, list);
    anariCommitParameters(d, d);

    THEN("only those are advertised")
    {
      REQUIRE(extensionsOf(d)
          == std::vector<std::string>{
              "ANARI_KHR_CAMERA_PERSPECTIVE", "ANARI_KHR_GEOMETRY_TRIANGLE"});
    }

    THEN("the capture records them")
    {
      Scene scene(d);
      REQUIRE(scene.render().find("device {\n"
                                  "  extensions ANARI_STRING_LIST [\n"
                                  "    \"ANARI_KHR_CAMERA_PERSPECTIVE\"\n"
                                  "    \"ANARI_KHR_GEOMETRY_TRIANGLE\"\n"
                                  "  ]\n"
                                  "}\n")
          != std::string::npos);
    }
  }

  WHEN("an empty list of extensions is set")
  {
    const char *list[] = {nullptr};
    anariSetParameter(d, d, "extensions", ANARI_STRING_LIST, list);
    anariCommitParameters(d, d);

    THEN("none is advertised")
    {
      REQUIRE(extensionsOf(d).empty());
      Scene scene(d);
      REQUIRE(scene.render().find("  extensions ANARI_STRING_LIST []\n")
          != std::string::npos);
    }
  }
}

SCENARIO(
    "the device answers queries from the spec definitions", "[capture_device]")
{
  CaptureDevice cd;
  auto d = cd.device;

  THEN("it lists the subtypes of an object type")
  {
    std::vector<std::string> subtypes;
    for (auto **s = anariGetObjectSubtypes(d, ANARI_GEOMETRY); s && *s; ++s)
      subtypes.push_back(*s);
    REQUIRE(has(subtypes, "triangle"));
    REQUIRE(has(subtypes, "sphere"));
  }

  THEN("it describes the parameters of a subtype")
  {
    const auto *params = (const ANARIParameter *)anariGetObjectInfo(
        d, ANARI_CAMERA, "perspective", "parameter", ANARI_PARAMETER_LIST);
    REQUIRE(params != nullptr);
    std::vector<std::string> names;
    for (; params->name; ++params)
      names.push_back(params->name);
    REQUIRE(has(names, "fovy"));
    REQUIRE(has(names, "position"));
  }

  THEN("it describes its own parameters")
  {
    const auto *limit = (const uint32_t *)anariGetParameterInfo(d,
        ANARI_DEVICE,
        nullptr,
        "inlineArrayLimit",
        ANARI_UINT32,
        "default",
        ANARI_UINT32);
    REQUIRE(limit != nullptr);
    REQUIRE(*limit == 64);
  }
}

SCENARIO("a capture records spec violations as warnings", "[capture_device]")
{
  CaptureDevice cd;
  Scene scene(cd.device);
  auto d = cd.device;

  WHEN("an object has a subtype the spec does not define")
  {
    auto geometry = anari::newObject<anari::Geometry>(d, "blob");
    anari::commitParameters(d, geometry);
    Content content;
    attachObject(scene, content, "geometry", geometry);
    const auto capture = scene.render();
    const std::string warning = "geometry@0: unknown subtype \"blob\"";

    THEN("the capture ends with a warnings block")
    {
      const std::string tail =
          "\nwarnings {\n  " + quoteForTest(warning) + "\n}\n";
      REQUIRE(capture.size() > tail.size());
      REQUIRE(capture.substr(capture.size() - tail.size()) == tail);
    }

    THEN("the device reports the same message")
    {
      REQUIRE(cd.status.warnings == std::vector<std::string>{warning});
    }
    anari::release(d, geometry);
  }

  WHEN("an object has a parameter the spec does not define")
  {
    anari::setParameter(d, scene.world, "extraStuff", 3);
    scene.commit(scene.world);
    scene.render();

    THEN("it is reported")
    {
      REQUIRE(cd.status.warnings
          == std::vector<std::string>{
              "world@0: unknown parameter \"extraStuff\""});
    }
  }

  WHEN("a parameter has the wrong type")
  {
    anari::setParameter(d, scene.camera, "fovy", 3);
    scene.commit(scene.camera);
    const auto capture = scene.render();

    THEN("it is reported, and the value is still captured")
    {
      REQUIRE(cd.status.warnings
          == std::vector<std::string>{"camera@0: parameter \"fovy\" has type "
                                      "ANARI_INT32, expected ANARI_FLOAT32"});
      REQUIRE(capture.find("  fovy ANARI_INT32 3\n") != std::string::npos);
    }
  }

  WHEN("a reachable array is still mapped at render time")
  {
    auto array = anari::newArray1D(d, anari::DataType(ANARI_FLOAT32), 2);
    Content content;
    attachArray(scene, content, "primitive.attribute0", array);
    anari::map<void>(d, array);
    scene.render();

    THEN("it is reported")
    {
      REQUIRE(cd.status.warnings
          == std::vector<std::string>{
              "array1d@2: array is still mapped at render time"});
    }

    anari::unmap(d, array);
    anari::release(d, array);
  }

  WHEN("an unreachable object breaks the rules")
  {
    auto stray = anari::newObject<anari::Camera>(d, "blob");
    anari::setParameter(d, stray, "nonsense", 1);
    anari::commitParameters(d, stray);
    scene.render();

    THEN("nothing is reported")
    {
      REQUIRE(cd.status.warnings.empty());
    }
    anari::release(d, stray);
  }

  WHEN("a renderer has parameters only a particular device defines")
  {
    anari::setParameter(d, scene.renderer, "pixelSamples", 4);
    scene.commit(scene.renderer);
    scene.render();

    THEN("they are not reported")
    {
      REQUIRE(cd.status.warnings.empty());
    }
  }
}

namespace {

struct Mapped
{
  const void *data{nullptr};
  uint32_t width{0};
  uint32_t height{0};
  ANARIDataType type{ANARI_UNKNOWN};
};

Mapped mapChannel(anari::Device d, anari::Frame f, const char *channel)
{
  Mapped m;
  m.data = anariMapFrame(d, f, channel, &m.width, &m.height, &m.type);
  return m;
}

// The bytes of 'm', as 'T's
template <typename T>
std::vector<T> pixels(const Mapped &m)
{
  const size_t n =
      size_t(m.width) * m.height * anari::sizeOf(m.type) / sizeof(T);
  const T *p = (const T *)m.data;
  return std::vector<T>(p, p + n);
}

// A frame of the given size that maps the color channel as 'colorType', and
// the depth channel as FLOAT32 if 'depth'
void declareChannels(Scene &scene,
    anari::math::uint2 size,
    ANARIDataType colorType,
    bool depth = false)
{
  auto d = scene.d;
  anari::setParameter(d, scene.frame, "size", size);
  if (colorType != ANARI_UNKNOWN)
    anari::setParameter(d, scene.frame, "channel.color", colorType);
  if (depth)
    anari::setParameter(d, scene.frame, "channel.depth", ANARI_FLOAT32);
  anari::commitParameters(d, scene.frame);
}

} // namespace

SCENARIO("a frame maps its channels as a checkerboard test pattern",
    "[capture_device]")
{
  CaptureDevice cd;
  Scene scene(cd.device);
  auto d = cd.device;

  GIVEN("a 4x4 frame with 2x2 cells and two colors")
  {
    anari::setParameter(d, scene.frame, "capture.cellSize", uint32_t(2));
    anari::setParameter(
        d, scene.frame, "capture.color0", anari::math::float3(1.f, 0.5f, 0.f));
    anari::setParameter(
        d, scene.frame, "capture.color1", anari::math::float3(0.f, 0.f, 1.f));
    anari::setParameter(d, scene.frame, "capture.alpha", 0.5f);

    // Which of the two colors the pixel at (x, y) is
    auto cell = [](int x, int y) { return ((x / 2) + (y / 2)) % 2; };

    WHEN("the color channel is mapped as FLOAT32_VEC4")
    {
      declareChannels(scene, {4, 4}, ANARI_FLOAT32_VEC4);
      scene.render();
      const auto m = mapChannel(d, scene.frame, "channel.color");

      THEN("each pixel is a color with the alpha, exactly")
      {
        REQUIRE(m.width == 4);
        REQUIRE(m.height == 4);
        REQUIRE(m.type == ANARI_FLOAT32_VEC4);
        const auto px = pixels<float>(m);
        REQUIRE(px.size() == 4 * 4 * 4);
        for (int y = 0; y < 4; ++y) {
          for (int x = 0; x < 4; ++x) {
            const float *p = &px[(y * 4 + x) * 4];
            const std::vector<float> expected = cell(x, y) == 0
                ? std::vector<float>{1.f, 0.5f, 0.f, 0.5f}
                : std::vector<float>{0.f, 0.f, 1.f, 0.5f};
            REQUIRE(std::vector<float>(p, p + 4) == expected);
          }
        }
      }
      anariUnmapFrame(d, scene.frame, "channel.color");
    }

    WHEN("the color channel is mapped as UFIXED8_VEC4")
    {
      declareChannels(scene, {4, 4}, ANARI_UFIXED8_VEC4);
      scene.render();
      const auto m = mapChannel(d, scene.frame, "channel.color");

      THEN("components are rounded to the nearest code")
      {
        REQUIRE(m.type == ANARI_UFIXED8_VEC4);
        const auto px = pixels<uint8_t>(m);
        REQUIRE(px.size() == 4 * 4 * 4);
        for (int y = 0; y < 4; ++y) {
          for (int x = 0; x < 4; ++x) {
            const uint8_t *p = &px[(y * 4 + x) * 4];
            const std::vector<uint8_t> expected = cell(x, y) == 0
                ? std::vector<uint8_t>{255, 128, 0, 128}
                : std::vector<uint8_t>{0, 0, 255, 128};
            REQUIRE(std::vector<uint8_t>(p, p + 4) == expected);
          }
        }
      }
      anariUnmapFrame(d, scene.frame, "channel.color");
    }

    WHEN("the color channel is mapped as UFIXED8_RGBA_SRGB")
    {
      declareChannels(scene, {4, 4}, ANARI_UFIXED8_RGBA_SRGB);
      scene.render();
      const auto m = mapChannel(d, scene.frame, "channel.color");

      THEN("color is sRGB encoded and alpha is not")
      {
        REQUIRE(m.type == ANARI_UFIXED8_RGBA_SRGB);
        const auto px = pixels<uint8_t>(m);
        const uint8_t *first = &px[0];
        REQUIRE(std::vector<uint8_t>(first, first + 4)
            == std::vector<uint8_t>{255, 188, 0, 128});
        const uint8_t *second = &px[(0 * 4 + 2) * 4];
        REQUIRE(std::vector<uint8_t>(second, second + 4)
            == std::vector<uint8_t>{0, 0, 255, 128});
      }
      anariUnmapFrame(d, scene.frame, "channel.color");
    }

    WHEN("the mode is solid")
    {
      anari::setParameter(d, scene.frame, "capture.mode", "solid");
      declareChannels(scene, {4, 4}, ANARI_UFIXED8_VEC4);
      scene.render();
      const auto m = mapChannel(d, scene.frame, "channel.color");

      THEN("every pixel is the first color")
      {
        const auto px = pixels<uint8_t>(m);
        REQUIRE(px.size() == 4 * 4 * 4);
        for (size_t i = 0; i < px.size(); i += 4) {
          REQUIRE(std::vector<uint8_t>(&px[i], &px[i] + 4)
              == std::vector<uint8_t>{255, 128, 0, 128});
        }
      }
      anariUnmapFrame(d, scene.frame, "channel.color");
    }
  }

  GIVEN("a frame with the default pattern")
  {
    declareChannels(scene, {16, 8}, ANARI_UFIXED8_VEC4);
    scene.render();
    const auto m = mapChannel(d, scene.frame, "channel.color");

    THEN("cells are 8 pixels wide, white then black, opaque")
    {
      const auto px = pixels<uint8_t>(m);
      REQUIRE(px.size() == 16 * 8 * 4);
      auto at = [&](int x, int y) {
        return std::vector<uint8_t>(
            &px[(y * 16 + x) * 4], &px[(y * 16 + x) * 4] + 4);
      };
      REQUIRE(at(0, 0) == std::vector<uint8_t>{255, 255, 255, 255});
      REQUIRE(at(7, 7) == std::vector<uint8_t>{255, 255, 255, 255});
      REQUIRE(at(8, 0) == std::vector<uint8_t>{0, 0, 0, 255});
      REQUIRE(at(15, 7) == std::vector<uint8_t>{0, 0, 0, 255});
    }
    anariUnmapFrame(d, scene.frame, "channel.color");
  }

  GIVEN("a frame that maps depth")
  {
    anari::setParameter(d, scene.frame, "capture.depthNear", 1.f);
    anari::setParameter(d, scene.frame, "capture.depthFar", 2.f);
    declareChannels(scene, {4, 2}, ANARI_UNKNOWN, true);
    scene.render();
    const auto m = mapChannel(d, scene.frame, "channel.depth");

    THEN("depth ramps linearly across each row, the same in every row")
    {
      REQUIRE(m.type == ANARI_FLOAT32);
      REQUIRE(m.width == 4);
      REQUIRE(m.height == 2);
      const auto px = pixels<float>(m);
      REQUIRE(px
          == std::vector<float>{
              1.125f, 1.375f, 1.625f, 1.875f, 1.125f, 1.375f, 1.625f, 1.875f});
    }
    anariUnmapFrame(d, scene.frame, "channel.depth");
  }

  GIVEN("a frame that declares other channels")
  {
    anari::setParameter(d, scene.frame, "channel.primitiveId", ANARI_UINT32);
    declareChannels(scene, {4, 4}, ANARI_UNKNOWN);
    scene.render();

    THEN("they map as zeros, and undeclared channels do not map")
    {
      const auto m = mapChannel(d, scene.frame, "channel.primitiveId");
      REQUIRE(m.type == ANARI_UINT32);
      REQUIRE(m.width == 4);
      REQUIRE(m.height == 4);
      const auto px = pixels<uint32_t>(m);
      REQUIRE(px == std::vector<uint32_t>(16, 0u));
      anariUnmapFrame(d, scene.frame, "channel.primitiveId");

      REQUIRE(mapChannel(d, scene.frame, "channel.objectId").data == nullptr);
      REQUIRE(mapChannel(d, scene.frame, "channel.color").data == nullptr);
    }
  }

  THEN("the pattern parameters are not spec violations")
  {
    anari::setParameter(d, scene.frame, "capture.mode", "checkerboard");
    anari::setParameter(d, scene.frame, "capture.cellSize", uint32_t(4));
    anari::setParameter(
        d, scene.frame, "capture.color0", anari::math::float3(1.f));
    anari::setParameter(
        d, scene.frame, "capture.color1", anari::math::float3(0.f));
    anari::setParameter(d, scene.frame, "capture.alpha", 1.f);
    anari::setParameter(d, scene.frame, "capture.depthNear", 0.f);
    anari::setParameter(d, scene.frame, "capture.depthFar", 1.f);
    declareChannels(scene, {4, 4}, ANARI_UFIXED8_VEC4, true);
    scene.render();
    REQUIRE(cd.status.warnings.empty());
    REQUIRE(cd.status.errors.empty());
  }
}

namespace {

namespace fs = std::filesystem;

// A fresh directory that is removed again (or, with 'create' false, only its
// name, for a directory the device is to make)
struct TempDir
{
  fs::path path;

  explicit TempDir(bool create = true)
  {
    static int counter = 0;
    path = fs::temp_directory_path()
        / ("anari_capture_test_" + std::to_string(counter++) + "_"
            + std::to_string(std::rand()));
    fs::remove_all(path);
    if (create)
      fs::create_directories(path);
  }

  ~TempDir()
  {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

std::string readFile(const fs::path &p)
{
  std::ifstream in(p, std::ios::binary);
  return std::string(
      std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<std::string> filesIn(const fs::path &dir)
{
  std::vector<std::string> names;
  if (fs::exists(dir)) {
    for (auto &e : fs::directory_iterator(dir)) {
      if (e.is_regular_file())
        names.push_back(e.path().filename().string());
    }
  }
  std::sort(names.begin(), names.end());
  return names;
}

void setEnv(const char *name, const char *value)
{
#ifdef _WIN32
  _putenv_s(name, value ? value : "");
#else
  if (value)
    setenv(name, value, 1);
  else
    unsetenv(name);
#endif
}

// Sets an environment variable for the life of the object
struct ScopedEnv
{
  std::string name;
  std::string old;
  bool hadOld;

  ScopedEnv(const char *n, const char *value) : name(n)
  {
    const char *current = std::getenv(n);
    hadOld = current != nullptr;
    if (hadOld)
      old = current;
    setEnv(n, value);
  }

  ~ScopedEnv()
  {
    setEnv(name.c_str(), hadOld ? old.c_str() : nullptr);
  }
};

void setCaptureDir(anari::Device d, const fs::path &dir)
{
  anari::setParameter(d, d, "captureDir", dir.string());
  anari::commitParameters(d, d);
}

} // namespace

SCENARIO("captures are written to a directory, one file per render",
    "[capture_device]")
{
  ScopedEnv noEnv("ANARI_CAPTURE_DIR", nullptr);
  CaptureDevice cd;
  auto d = cd.device;
  TempDir dir;

  GIVEN("a capture directory set on the device")
  {
    setCaptureDir(d, dir.path);
    Scene scene(d);

    WHEN("a frame renders twice")
    {
      const auto first = scene.render();
      anari::setParameter(d, scene.camera, "fovy", 0.5f);
      scene.commit(scene.camera);
      const auto second = scene.render();

      THEN("each render has a file, named by the frame and the render")
      {
        REQUIRE(filesIn(dir.path)
            == std::vector<std::string>{"frame0000_render0000.capture",
                "frame0000_render0001.capture"});
      }

      THEN("each file holds exactly the capture property")
      {
        REQUIRE(readFile(dir.path / "frame0000_render0000.capture") == first);
        REQUIRE(readFile(dir.path / "frame0000_render0001.capture") == second);
        REQUIRE(first != second);
      }
    }

    WHEN("a second frame renders")
    {
      scene.render();
      Scene other(d);
      other.render();

      THEN("its files are numbered by its place among the frames")
      {
        REQUIRE(filesIn(dir.path)
            == std::vector<std::string>{"frame0000_render0000.capture",
                "frame0001_render0000.capture"});
      }
    }

    WHEN("a capture has blobs")
    {
      std::vector<float> values(100);
      for (size_t i = 0; i < values.size(); ++i)
        values[i] = float(i) * 0.25f;
      Content content;
      auto array = content.keep(copyArray1D(d, values.data(), 100));
      attachArray(scene, content, "primitive.attribute0", array);
      const auto capture = scene.render();

      THEN("they are files in blobs/, named by the capture, with their bytes")
      {
        const auto names = blobNames(d, scene.frame);
        REQUIRE(names.size() == 1);
        REQUIRE(capture.find("  blob " + names[0] + "\n") != std::string::npos);
        REQUIRE(filesIn(dir.path / "blobs")
            == std::vector<std::string>{names[0] + ".bin"});
        REQUIRE(readFile(dir.path / "blobs" / (names[0] + ".bin"))
            == getBlob(d, scene.frame, names[0]));
      }
    }
  }

  GIVEN("a capture directory that does not exist yet")
  {
    TempDir missing(false);
    setCaptureDir(d, missing.path / "deeper" / "still");
    Scene scene(d);
    scene.render();

    THEN("it is created")
    {
      REQUIRE(filesIn(missing.path / "deeper" / "still")
          == std::vector<std::string>{"frame0000_render0000.capture"});
    }
  }

  GIVEN("a capture directory that cannot be written")
  {
    // A file where a directory is needed
    std::ofstream(dir.path / "file") << "x";
    setCaptureDir(d, dir.path / "file");
    Scene scene(d);
    const auto capture = scene.render();

    THEN("an error is reported, and the capture is still available")
    {
      REQUIRE(cd.status.errors.size() == 1);
      REQUIRE(capture.rfind("anari-capture 1\n", 0) == 0);
    }
  }

  GIVEN("no capture directory")
  {
    Scene scene(d);
    scene.render();

    THEN("nothing is reported")
    {
      REQUIRE(cd.status.errors.empty());
      REQUIRE(cd.status.warnings.empty());
    }
  }
}

SCENARIO("ANARI_CAPTURE_DIR names the capture directory when none is set",
    "[capture_device]")
{
  TempDir envDir;
  TempDir deviceDir;
  ScopedEnv env("ANARI_CAPTURE_DIR", envDir.path.string().c_str());
  CaptureDevice cd;
  auto d = cd.device;

  GIVEN("a device that sets no directory")
  {
    Scene scene(d);
    scene.render();

    THEN("captures go to the directory in the environment")
    {
      REQUIRE(filesIn(envDir.path)
          == std::vector<std::string>{"frame0000_render0000.capture"});
    }
  }

  GIVEN("a device that sets a directory too")
  {
    setCaptureDir(d, deviceDir.path);
    Scene scene(d);
    scene.render();

    THEN("the device's directory wins")
    {
      REQUIRE(filesIn(deviceDir.path)
          == std::vector<std::string>{"frame0000_render0000.capture"});
      REQUIRE(filesIn(envDir.path).empty());
    }
  }
}

namespace {

// The frame's last capture as the diff takes it: the text, and the blobs held
// in memory
anari::capture_diff::Capture capturedIn(anari::Device d, anari::Frame f)
{
  anari::capture_diff::Capture c;
  c.text = captureOf(d, f);
  for (const auto &name : blobNames(d, f)) {
    const auto bytes = getBlob(d, f, name);
    c.blobs[name].assign(bytes.begin(), bytes.end());
  }
  return c;
}

} // namespace

SCENARIO("two renders of the same scene, made in another order, do not differ",
    "[capture_device][capture_diff]")
{
  namespace diff = anari::capture_diff;
  CaptureDevice cd;
  auto d = cd.device;

  diff::Capture first, second;
  {
    Scene scene(d);
    Content content;
    buildSharedMaterialScene(scene, content, false);
    scene.render();
    first = capturedIn(d, scene.frame);
  }
  {
    Scene scene(d);
    Content content;
    buildSharedMaterialScene(scene, content, true);
    scene.render();
    second = capturedIn(d, scene.frame);
  }

  THEN("the diff of their captures is empty")
  {
    const auto result = diff::diffCaptures(first, second);
    REQUIRE(result.report() == "the captures match\n");
    REQUIRE(result.outcome == diff::Outcome::MATCH);
  }

  THEN("a change of one parameter is found by its path")
  {
    Scene scene(d);
    Content content;
    buildSharedMaterialScene(scene, content, false);
    anari::setParameter(d, scene.camera, "fovy", 0.25f);
    scene.commit(scene.camera);
    scene.render();
    const auto changed = capturedIn(d, scene.frame);
    const auto result = diff::diffCaptures(first, changed);
    REQUIRE(result.differences.size() == 1);
    REQUIRE(result.differences[0].message()
        == "camera.fovy: expected <absent>, got ANARI_FLOAT32 0.25");
  }
}

SCENARIO("captures with blobs are compared in memory and as files",
    "[capture_device][capture_diff]")
{
  namespace diff = anari::capture_diff;
  ScopedEnv noEnv("ANARI_CAPTURE_DIR", nullptr);
  CaptureDevice cd;
  auto d = cd.device;
  TempDir dir;
  setCaptureDir(d, dir.path);

  std::vector<float> values(300);
  for (size_t i = 0; i < values.size(); ++i)
    values[i] = float(i) * 0.5f;

  auto render = [&](float jitter) {
    auto copy = values;
    copy[123] += jitter;
    Scene scene(d);
    Content content;
    auto array = content.keep(copyArray1D(d, copy.data(), 300));
    attachArray(scene, content, "primitive.attribute0", array);
    scene.render();
    return capturedIn(d, scene.frame);
  };

  const auto base = render(0.f);
  const auto nearby = render(0.001f);
  REQUIRE(base.blobs.size() == 1);

  THEN("a blob that differs is compared element by element, with a tolerance")
  {
    REQUIRE(diff::diffCaptures(base, nearby).outcome == diff::Outcome::DIFFER);
    diff::DiffOptions options;
    options.tolerance.absolute = 0.01;
    REQUIRE(diff::diffCaptures(base, nearby, options).outcome
        == diff::Outcome::MATCH);
  }

  THEN("the files written for the two renders compare the same way")
  {
    const auto a = (dir.path / "frame0000_render0000.capture").string();
    const auto b = (dir.path / "frame0001_render0000.capture").string();
    const auto result = diff::diffCaptureFiles(a, b);
    REQUIRE(result.outcome == diff::Outcome::DIFFER);
    REQUIRE(result.differences.size() == 1);
    REQUIRE(result.differences[0].message().rfind(
                "world.instance[0].group.surface[0].geometry."
                "primitive.attribute0[123]: expected 61.5, got 61.501",
                0)
        == 0);
    diff::DiffOptions options;
    options.tolerance.absolute = 0.01;
    REQUIRE(
        diff::diffCaptureFiles(a, b, options).outcome == diff::Outcome::MATCH);
  }
}
