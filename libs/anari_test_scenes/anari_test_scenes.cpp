// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "anari_test_scenes.h"
// std
#include <map>
#include <memory>
#include <string>
// scenes
#include "scenes/attributes.h"
#include "scenes/cornell_box.h"
#include "scenes/file_obj.h"
#include "scenes/gravity_spheres_volume.h"
#include "scenes/instanced_cubes.h"
#include "scenes/pbr_spheres.h"
#include "scenes/random_cylinders.h"
#include "scenes/random_spheres.h"
#include "scenes/textured_cube.h"

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

    registerScene("default", "random_spheres", sceneRandomSpheres);
    registerScene("default", "cornell_box", sceneCornellBox);
    registerScene(
        "default", "gravity_spheres_volume", sceneGravitySphereVolume);
    registerScene("default", "instanced_cubes", sceneInstancedCubes);
    registerScene("default", "textured_cube", sceneTexturedCube);
    registerScene("default", "random_cylinders", sceneRandomCylinders);
    registerScene("default", "triangle_attributes", sceneAttributes);
    registerScene("default", "pbr_spheres", scenePbrSpheres);
    registerScene("default", "file_obj", sceneFileObj);
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
