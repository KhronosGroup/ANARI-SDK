// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "LightsEditor.h"
// std
#include <cmath>

namespace windows {

LightsEditor::LightsEditor(std::vector<anari::Device> devices, const char *name)
    : Window(name, true), m_devices(devices)
{
  for (auto d : m_devices)
    anari::retain(d, d);
  addNewLight(true);
  m_worlds.resize(m_devices.size(), nullptr);
}

LightsEditor::LightsEditor(anari::Device device, const char *name)
    : LightsEditor(std::vector<anari::Device>{device}, name)
{}

LightsEditor::~LightsEditor()
{
  releaseWorlds();
  for (auto &l : m_lights) {
    for (int i = 0; i < int(m_devices.size()); i++)
      anari::release(m_devices[i], l.handles[i]);
  }
  for (auto d : m_devices)
    anari::release(d, d);
}

void LightsEditor::buildUI()
{
  ImGui::PushItemWidth(250);

  ImGui::Text("add:");
  ImGui::SameLine();
  if (ImGui::Button("directional"))
    addNewLight(true);
  ImGui::SameLine();
  if (ImGui::Button("point"))
    addNewLight(false);

  Light *lightToRemove = nullptr;

  for (auto &l : m_lights) {
    ImGui::PushID(&l);
    ImGui::Separator();

    ImGui::Text("type: %s", l.isDirectional ? "directional" : "point");

    bool update = false;

    update |= ImGui::DragFloat("intensity", &l.intensity, 0.001f, 0.f, 1000.f);

    update |= ImGui::ColorEdit3("color", &l.color.x);

    if (l.isDirectional) {
      auto maintainUnitCircle = [](float inDegrees) -> float {
        while (inDegrees > 360.f)
          inDegrees -= 360.f;
        while (inDegrees < 0.f)
          inDegrees += 360.f;
        return inDegrees;
      };

      if (ImGui::DragFloat("azimuth", &l.directionalAZEL.x, 0.1f)) {
        update = true;
        l.directionalAZEL.x = maintainUnitCircle(l.directionalAZEL.x);
      }

      if (ImGui::DragFloat("elevation", &l.directionalAZEL.y, 0.1f)) {
        update = true;
        l.directionalAZEL.y = maintainUnitCircle(l.directionalAZEL.y);
      }
    } else { // point
      if (ImGui::DragFloat3("position", &l.pointPosition.x, 0.1f))
        update = true;
    }

    if (ImGui::Button("remove"))
      lightToRemove = &l;

    if (update)
      updateLight(l);

    ImGui::PopID();
  }

  if (lightToRemove != nullptr)
    removeLight(lightToRemove);

  ImGui::PopItemWidth();
}

void LightsEditor::setWorld(anari::World world)
{
  assert(m_devices.size() == 1);
  setWorlds({world});
}

void LightsEditor::setWorlds(std::vector<anari::World> worlds)
{
  releaseWorlds();
  for (int i = 0; i < int(m_devices.size()); i++)
    anari::retain(m_devices[i], worlds[i]);
  m_worlds = worlds;
  updateLightsArray();
}

void LightsEditor::releaseWorlds()
{
  for (int i = 0; i < int(m_devices.size()); i++)
    anari::release(m_devices[i], m_worlds[i]);
}

void LightsEditor::addNewLight(bool directional)
{
  Light l;
  l.isDirectional = directional;
  for (int i = 0; i < int(m_devices.size()); i++) {
    l.handles.push_back(anari::newObject<anari::Light>(
        m_devices[i], directional ? "directional" : "point"));
  }
  updateLight(l);
  m_lights.push_back(l);
  updateLightsArray();
}

void LightsEditor::removeLight(Light *toRemove)
{
  for (int i = 0; i < int(m_devices.size()); i++)
    anari::release(m_devices[i], toRemove->handles[i]);

  m_lights.erase(std::remove_if(m_lights.begin(),
                     m_lights.end(),
                     [&](const auto &l) -> bool { return &l == toRemove; }),
      m_lights.end());

  updateLightsArray();
}

void LightsEditor::updateLight(const Light &l)
{
  for (int i = 0; i < int(m_devices.size()); i++) {
    auto device = m_devices[i];
    auto light = l.handles[i];
    anari::setParameter(device, light, "intensity", l.intensity);
    anari::setParameter(device, light, "irradiance", l.intensity);
    anari::setParameter(device, light, "color", l.color);

    auto radians = [](float degrees) -> float {
      return degrees * M_PI / 180.f;
    };

    if (l.isDirectional) {
      const float az = radians(l.directionalAZEL.x);
      const float el = radians(l.directionalAZEL.y);
      anari::float3 dir(std::sin(az) * std::cos(el),
          std::sin(el),
          std::cos(az) * std::cos(el));
      anari::setParameter(device, light, "direction", dir);
    } else {
      anari::setParameter(device, light, "position", l.pointPosition);
    }

    anari::commitParameters(device, light);
  }
}

void LightsEditor::updateLightsArray()
{
  if (m_worlds.empty() || !m_worlds[0])
    return;

  for (int i = 0; i < int(m_devices.size()); i++) {
    auto device = m_devices[i];
    auto world = m_worlds[i];
    if (m_lights.empty())
      anari::unsetParameter(device, world, "light");
    else {
      std::vector<anari::Light> tmp;
      for (auto &l : m_lights)
        tmp.push_back(l.handles[i]);
      anari::setAndReleaseParameter(device,
          world,
          "light",
          anari::newArray1D(device, tmp.data(), tmp.size()));
    }
    anari::commitParameters(device, world);
  }
}

} // namespace windows