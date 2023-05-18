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

std::vector<std::string> getAvailableSceneCategories();
std::vector<std::string> getAvailableSceneNames(const char *category);

// Create an instance of a scene
SceneHandle createScene(
    anari::Device d, const char *category, const char *name);

// Set a scene parameter to be applied on the next 'commit()'
void setParameter(SceneHandle s, const std::string &name, Any val);

// Apply all parameters on the scene (almost always regenerates the world)
void commit(SceneHandle s);

// Get the ANARI world handle from the underlying scene: do not need to release
anari::World getWorld(SceneHandle s);

// Get the world-space bounds of the underlying scene
Bounds getBounds(SceneHandle s);

// Get a list of all parameters which this scene can use
std::vector<ParameterInfo> getParameters(SceneHandle s);

// Get a list of all the camera positions the scene has embedded in it
std::vector<Camera> getCameras(SceneHandle s);

// Ask if the scene can be animated?
bool isAnimated(SceneHandle s);

// If the scene is animated, change the world to the next frame
void computeNextFrame(SceneHandle s);

// Free the underlying scene (and all ANARI objects created by it)
void release(SceneHandle s);

// Scene registration /////////////////////////////////////////////////////////

using SceneConstructorFcn = std::function<TestScene *(anari::Device)>;

void registerScene(const std::string &category,
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
