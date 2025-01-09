// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include "anari/anari_cpp.hpp"
// helium
#include "helium/helium_math.h"
#include "helium/utility/AnariAny.h"
#include "helium/utility/ParameterInfo.h"
// std
#include <array>
#include <functional>
#include <vector>

#include "anari_test_scenes_export.h"

namespace anari {
namespace scenes {

using Any = helium::AnariAny;
using ParameterInfo = helium::ParameterInfo;
using mat4 = math::mat4;

struct Camera
{
  math::float3 position;
  math::float3 direction;
  math::float3 at;
  math::float3 up;
};

using Bounds = std::array<math::float3, 2>;

struct TestScene;
using SceneHandle = TestScene *;

ANARI_TEST_SCENES_INTERFACE std::vector<std::string>
getAvailableSceneCategories();
ANARI_TEST_SCENES_INTERFACE std::vector<std::string> getAvailableSceneNames(
    const char *category);

// Create an instance of a scene
ANARI_TEST_SCENES_INTERFACE SceneHandle createScene(
    anari::Device d, const char *category, const char *name);

// Set a scene parameter to be applied on the next 'commit()'
ANARI_TEST_SCENES_INTERFACE void setParameter(
    SceneHandle s, const std::string &name, Any val);

// Apply all parameters on the scene (almost always regenerates the world)
ANARI_TEST_SCENES_INTERFACE void commit(SceneHandle s);

// Get the ANARI world handle from the underlying scene: do not need to release
ANARI_TEST_SCENES_INTERFACE anari::World getWorld(SceneHandle s);

// Get the world-space bounds of the underlying scene
ANARI_TEST_SCENES_INTERFACE Bounds getBounds(SceneHandle s);

// Get a list of all parameters which this scene can use
ANARI_TEST_SCENES_INTERFACE std::vector<ParameterInfo> getParameters(
    SceneHandle s);

// Get a list of all the camera positions the scene has embedded in it
ANARI_TEST_SCENES_INTERFACE std::vector<Camera> getCameras(SceneHandle s);

// Ask if the scene can be animated?
ANARI_TEST_SCENES_INTERFACE bool isAnimated(SceneHandle s);

// If the scene is animated, change the world to the next frame
ANARI_TEST_SCENES_INTERFACE void computeNextFrame(SceneHandle s);

// Free the underlying scene (and all ANARI objects created by it)
ANARI_TEST_SCENES_INTERFACE void release(SceneHandle s);

// Scene registration /////////////////////////////////////////////////////////

using SceneConstructorFcn = std::function<TestScene *(anari::Device)>;

ANARI_TEST_SCENES_INTERFACE void registerScene(const std::string &category,
    const std::string &name,
    SceneConstructorFcn ctor);

} // namespace scenes
} // namespace anari
