// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// match3D
#include <match3D/match3D.h>
// std
#include <vector>

namespace windows {

struct Light
{
  enum LightType
  {
    DIRECTIONAL,
    POINT,
    SPOT
  };
  LightType type{DIRECTIONAL};
  float intensity{1.f};
  float openingAngle{3.14159f};
  anari::float3 color{1.f};
  anari::float2 directionalAZEL{0.f, 345.f};
  anari::float3 pointPosition{0.f};
  std::vector<anari::Light> handles;
};

struct LightsEditor : public match3D::Window
{
  LightsEditor(
      std::vector<anari::Device> devices, const char *name = "Lights Editor");
  LightsEditor(anari::Device device, const char *name = "Lights Editor");
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

} // namespace windows
