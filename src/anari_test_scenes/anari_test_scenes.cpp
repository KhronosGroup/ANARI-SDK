// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "anari_test_scenes.h"
// std
#include <map>
#include <memory>
#include <string>
// scenes
#include "scenes/demo/cornell_box.h"
#include "scenes/demo/gravity_spheres_volume.h"
#include "scenes/file/obj.h"
#ifdef ENABLE_GLTF
#include "scenes/file/glTF.h"
#endif
#include "scenes/performance/materials.h"
#include "scenes/test/attributes.h"
#include "scenes/test/instanced_cubes.h"
#include "scenes/test/pbr_spheres.h"
#include "scenes/test/random_cylinders.h"
#include "scenes/test/random_spheres.h"
#include "scenes/test/textured_cube.h"

namespace anari {
namespace scenes {

using FactoryMap = std::map<std::string, SceneConstructorFcn>;
using CategoryMap = std::map<std::string, FactoryMap>;
using CategoryMapPtr = std::unique_ptr<CategoryMap>;

static CategoryMapPtr g_scenes;

static void init()
{
  if (g_scenes.get() != nullptr)
    return;

  g_scenes.reset(new CategoryMap());

  {
    auto &scenes = *g_scenes;

    // demo scenes
    registerScene("demo", "cornell_box", sceneCornellBox);
    registerScene("demo", "gravity_spheres_volume", sceneGravitySphereVolume);

    // file loaders
    registerScene("file", "obj", sceneFileObj);
#ifdef ENABLE_GLTF
    registerScene("file", "glTF", sceneFileGLTF);
#endif

    // performance
    registerScene("perf", "materials", sceneMaterials);

    // tests
    registerScene("test", "random_spheres", sceneRandomSpheres);
    registerScene("test", "instanced_cubes", sceneInstancedCubes);
    registerScene("test", "textured_cube", sceneTexturedCube);
    registerScene("test", "random_cylinders", sceneRandomCylinders);
    registerScene("test", "triangle_attributes", sceneAttributes);
    registerScene("test", "pbr_spheres", scenePbrSpheres);
  }
}

std::vector<std::string> getAvailableSceneCategories()
{
  init();
  std::vector<std::string> categories;
  for (auto &v : *g_scenes)
    categories.push_back(v.first);
  return categories;
}

std::vector<std::string> getAvailableSceneNames(const char *category)
{
  init();
  std::vector<std::string> names;
  for (auto &v : (*g_scenes)[category])
    names.push_back(v.first);
  return names;
}

SceneHandle createScene(anari::Device d, const char *category, const char *name)
{
  init();
  auto &fcn = (*g_scenes)[category][name];
  return fcn ? fcn(d) : nullptr;
}

void setParameter(SceneHandle s, const std::string &name, Any val)
{
  s->setParamDirect(name, val);
}

void commit(SceneHandle s)
{
  s->commit();
}

anari::World getWorld(SceneHandle s)
{
  return s->world();
}

Bounds getBounds(SceneHandle s)
{
  return s->bounds();
}

std::vector<ParameterInfo> getParameters(SceneHandle s)
{
  return s->parameters();
}

std::vector<Camera> getCameras(SceneHandle s)
{
  return s->cameras();
}

bool isAnimated(SceneHandle s)
{
  return s->animated();
}

void computeNextFrame(SceneHandle s)
{
  s->computeNextFrame();
}

void release(SceneHandle s)
{
  delete s;
}

void registerScene(const std::string &category,
    const std::string &name,
    SceneConstructorFcn ctor)
{
  init();
  (*g_scenes)[category][name] = ctor;
}

} // namespace scenes
} // namespace anari
