// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Tests of the 'capture' device through the public ANARI API only: build a
// small scene, render, and check what comes out (the capture text, blobs,
// mapped frame contents, query answers, status messages).

#include "catch.hpp"

#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <cstdint>
#include <cstring>
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
