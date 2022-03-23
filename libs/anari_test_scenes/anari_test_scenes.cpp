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
#include "scenes/volume_test.h"
#include "scenes/cornell_box_directional.h"
#include "scenes/cornell_box_point.h"
#include "scenes/cornell_box_spot.h"
#include "scenes/cornell_box_ring.h"
#include "scenes/cornell_box_quad.h"
#include "scenes/cornell_box_hdri.h"
#include "scenes/cornell_box_multilight.h"
#include "scenes/cornell_box_quad_geom.h"
#include "scenes/cornell_box_cone_geom.h"
#include "scenes/cornell_box_cylinder_geom.h"
#include "scenes/textured_cube_samplers.h"

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
    scenes["volume_test"] = &sceneVolumeTest;
    scenes["cornell_box_directional"] = &sceneCornellBoxDirectional;
    scenes["cornell_box_point"] = &sceneCornellBoxPoint;
    scenes["cornell_box_spot"] = &sceneCornellBoxSpot;
    scenes["cornell_box_ring"] = &sceneCornellBoxRing;
    scenes["cornell_box_quad"] = &sceneCornellBoxQuad;
    scenes["cornell_box_hdri"] = &sceneCornellBoxHDRI;
    scenes["cornell_box_multilight"] = &sceneCornellBoxMultilight;
    scenes["textured_cube_samplers"] = &sceneTexturedCubeSamplers;
    scenes["cornell_box_quad_geom"] = &sceneCornellBoxQuadGeometry;
    scenes["cornell_box_cone_geom"] = &sceneCornellBoxCone;
    scenes["cornell_box_cylinder_geom"] = &sceneCornellBoxCylinder;
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
