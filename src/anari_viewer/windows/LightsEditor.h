// Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <string>
#include <vector>

#include "Window.h"

namespace anari_viewer::windows {

struct Light
{
  enum LightType
  {
    DIRECTIONAL,
    POINT,
    SPOT,
    HDRI
  };
  LightType type{DIRECTIONAL};
  float intensity{1.f};
  float openingAngle{3.14159f};
  anari::math::float3 color{1.f};
  anari::math::float2 directionalAZEL{0.f, 345.f};
  anari::math::float3 pointPosition{0.f};
  std::string hdriRadiance;
  float scale{1.f};
  std::vector<anari::Light> handles;
};

struct LightsEditor : public Window
{
  LightsEditor(Application *app,
      std::vector<anari::Device> devices, const char *name = "Lights Editor");
  LightsEditor(Application *app, anari::Device device, const char *name = "Lights Editor");
  ~LightsEditor();

  void buildUI() override;

  void setWorld(anari::World world);
  void setWorlds(std::vector<anari::World> worlds);

 private:
  void releaseWorlds();
  void addNewLight(Light::LightType type);
  void removeLight(Light *toRemove);
  void updateLight(const Light &l);
  void updateLightsArray();

  // ANARI //

  std::vector<anari::Device> m_devices;
  std::vector<anari::World> m_worlds;
  std::vector<Light> m_lights;
};

} // namespace anari_viewer::windows
