// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "anari_test_scenes.h"
// std
#include <map>
#include <memory>
#include <string>
// scenes
#include "scenes/cornell_box.h"
#include "scenes/file_obj.h"
#include "scenes/gravity_spheres_volume.h"
#include "scenes/instanced_cubes.h"
#include "scenes/random_spheres.h"
#include "scenes/textured_cube.h"

namespace anari {
namespace scenes {

using FactoryMap = std::map<std::string, TestScene *(*)(anari::Device)>;
using FactoryPtr = std::unique_ptr<FactoryMap>;

static FactoryPtr g_scenes;

static void init()
{
  g_scenes.reset(new FactoryMap());

  {
    auto &scenes = *g_scenes;

    scenes["cornell_box"] = &sceneCornellBox;
    scenes["gravity_spheres_volume"] = &sceneGravitySphereVolume;
    scenes["instanced_cubes"] = &sceneInstancedCubes;
    scenes["textured_cube"] = &sceneTexturedCube;
    scenes["random_spheres"] = &sceneRandomSpheres;
    scenes["file_obj"] = &sceneFileObj;
  }
}

SceneHandle createScene(anari::Device d, const char *name)
{
  if (g_scenes.get() == nullptr)
    init();

  auto *fcn = (*g_scenes)[name];

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

} // namespace scenes
} // namespace anari
