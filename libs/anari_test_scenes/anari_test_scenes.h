// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include "anari/anari_cpp.hpp"
#include "anari/backend/utilities/Any.h"
// anari-glm
#include "anari/anari_cpp/ext/glm.h"
// std
#include <array>
#include <vector>

namespace anari {
namespace scenes {

struct Camera
{
  glm::vec3 position;
  glm::vec3 direction;
  glm::vec3 at;
  glm::vec3 up;
};

struct ParameterInfo
{
  std::string name;
  ANARIDataType type;
  Any value;
  std::string description;
};

using Bounds = std::array<glm::vec3, 2>;

struct TestScene;
using SceneHandle = TestScene *;

// Create an instance of a scene
SceneHandle createScene(anari::Device d, const char *name);

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

} // namespace scenes
} // namespace anari
