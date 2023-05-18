// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include "anari/anari_cpp.hpp"
// helium
#include "helium/utility/AnariAny.h"
// anari-linalg
#include "anari/anari_cpp/ext/linalg.h"
// std
#include <array>
#include <functional>
#include <vector>

#include "anari_test_scenes_export.h"

namespace anari {
namespace scenes {

using Any = helium::AnariAny;
using mat4 = anari::mat4;

struct Camera
{
  anari::float3 position;
  anari::float3 direction;
  anari::float3 at;
  anari::float3 up;
};

struct ParameterInfo
{
  std::string name;
  Any value;
  Any min;
  Any max;
  std::string description;

  // valid values if this parameter is ANARI_STRING_LIST
  std::vector<std::string> stringValues;
  // which string is selected in 'stringValues', if applicable
  int currentSelection{0};
};

template <typename T>
ParameterInfo makeParameterInfo(
    const char *name, const char *description, T value);

template <typename T>
ParameterInfo makeParameterInfo(
    const char *name, const char *description, T value, T min, T max);

template <typename T>
ParameterInfo makeParameterInfo(const char *name,
    const char *description,
    const char *value,
    std::vector<std::string> stringValues);

using Bounds = std::array<anari::float3, 2>;

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

///////////////////////////////////////////////////////////////////////////////
// Inlined definitions ////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

template <typename T>
inline ParameterInfo makeParameterInfo(
    const char *name, const char *description, T value)
{
  ParameterInfo retval;
  retval.name = name;
  retval.description = description;
  retval.value = value;
  return retval;
}

template <typename T>
inline ParameterInfo makeParameterInfo(
    const char *name, const char *description, T value, T min, T max)
{
  ParameterInfo retval;
  retval.name = name;
  retval.description = description;
  retval.value = value;
  retval.min = min;
  retval.max = max;
  return retval;
}

template <typename T>
inline ParameterInfo makeParameterInfo(const char *name,
    const char *description,
    const char *value,
    std::vector<std::string> stringValues)
{
  ParameterInfo retval;
  retval.name = name;
  retval.description = description;
  retval.value = value;
  retval.stringValues = stringValues;
  return retval;
}

} // namespace scenes
} // namespace anari
