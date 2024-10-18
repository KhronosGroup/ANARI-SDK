// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "LightsEditor.h"
#include "../HDRImage.h"
// anari_viewer
#include "nfd.h"
// std
#include <cmath>

namespace anari_viewer::windows {

static const char *lightToType(Light::LightType type)
{
  switch (type) {
  case Light::DIRECTIONAL:
    return "directional";
  case Light::POINT:
    return "point";
  case Light::SPOT:
    return "spot";
  case Light::HDRI:
    return "hdri";
  default:
    return nullptr;
  }
}

LightsEditor::LightsEditor(std::vector<anari::Device> devices, const char *name)
    : Window(name, true), m_devices(devices)
{
  for (auto d : m_devices)
    anari::retain(d, d);
  addNewLight(Light::DIRECTIONAL);
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
    addNewLight(Light::DIRECTIONAL);
  ImGui::SameLine();
  if (ImGui::Button("point"))
    addNewLight(Light::POINT);
  ImGui::SameLine();
  if (ImGui::Button("spot"))
    addNewLight(Light::SPOT);
  ImGui::SameLine();
  if (ImGui::Button("hdri"))
    addNewLight(Light::HDRI);

  Light *lightToRemove = nullptr;

  for (auto &l : m_lights) {
    ImGui::PushID(&l);
    ImGui::Separator();

    ImGui::Text("type: %s", lightToType(l.type));

    bool update = false;

    if (l.type != Light::HDRI) {
      update |=
          ImGui::DragFloat("intensity", &l.intensity, 0.001f, 0.f, 1000.f);

      update |= ImGui::ColorEdit3("color", &l.color.x);
    }

    if (l.type == Light::DIRECTIONAL || l.type == Light::SPOT) {
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
    }
    if (l.type == Light::POINT || l.type == Light::SPOT) {
      if (ImGui::DragFloat3("position", &l.pointPosition.x, 0.1f))
        update = true;
    }

    if (l.type == Light::SPOT) {
      if (ImGui::DragFloat(
              "openingAngle", &l.openingAngle, 0.01f, 0.0f, 6.2832f))
        update = true;
    }

    if (l.type == Light::HDRI) {
      const void *value = l.hdriRadiance.data();

      constexpr int MAX_LENGTH = 2000;
      l.hdriRadiance.reserve(MAX_LENGTH);

      if (ImGui::Button("...")) {
        nfdchar_t *outPath = nullptr;
        nfdfilteritem_t filterItem[1] = {{"HDR Image Files", "exr,hdr"}};
        nfdresult_t result = NFD_OpenDialog(&outPath, filterItem, 1, nullptr);
        if (result == NFD_OKAY) {
          l.hdriRadiance = std::string(outPath).c_str();
          NFD_FreePath(outPath);
        } else {
          printf("NFD Error: %s\n", NFD_GetError());
        }
      }

      ImGui::SameLine();

      auto text_cb = [](ImGuiInputTextCallbackData *cbd) {
        auto &l = *(Light *)cbd->UserData;
        l.hdriRadiance.resize(cbd->BufTextLen);
        return 0;
      };

      ImGui::InputText("fileName",
          (char *)value,
          MAX_LENGTH,
          ImGuiInputTextFlags_CallbackEdit,
          text_cb,
          &l);

      ImGui::DragFloat("scale", &l.scale, 0.01f, 10.f);

      update = ImGui::Button("update");
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

void LightsEditor::addNewLight(Light::LightType type)
{
  Light l;
  l.type = type;
  for (int i = 0; i < int(m_devices.size()); i++) {
    l.handles.push_back(
        anari::newObject<anari::Light>(m_devices[i], lightToType(type)));
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

    if (l.type == Light::DIRECTIONAL || l.type == Light::SPOT) {
      const float az = radians(l.directionalAZEL.x);
      const float el = radians(l.directionalAZEL.y);
      anari::math::float3 dir(std::sin(az) * std::cos(el),
          std::sin(el),
          std::cos(az) * std::cos(el));
      anari::setParameter(device, light, "direction", dir);
    }
    if (l.type == Light::POINT || l.type == Light::SPOT) {
      anari::setParameter(device, light, "position", l.pointPosition);
    }
    if (l.type == Light::SPOT) {
      anari::setParameter(device, light, "openingAngle", l.openingAngle);
    }
    if (l.type == Light::HDRI) {
      std::vector<float> pixel;
      unsigned width = 0, height = 0;
      importers::HDRImage hdrImg;
      if (hdrImg.load(l.hdriRadiance)) {
        pixel.resize(hdrImg.width * hdrImg.height * 3);
        width = hdrImg.width;
        height = hdrImg.height;
        if (hdrImg.numComponents == 3) {
          std::copy(hdrImg.pixel.begin(), hdrImg.pixel.end(), pixel.begin());
        } else if (hdrImg.numComponents == 4) {
          for (size_t i = 0; i < hdrImg.pixel.size(); i += 4) {
            size_t i3 = i / 4 * 3;
            pixel[i3] = hdrImg.pixel[i];
            pixel[i3 + 1] = hdrImg.pixel[i + 1];
            pixel[i3 + 2] = hdrImg.pixel[i + 2];
          }
        } else {
          printf("Error loading HDR image, unsupported num. components: %u\n",
              hdrImg.numComponents);
          pixel.clear();
          width = height = 0;
        }
      }

      if (pixel.empty()) {
        // Fill with dummy data:
        pixel.resize(6, 1.f);
        width = 2;
        height = 1;
      }

      ANARIArray2D radiance = anariNewArray2D(
          device, pixel.data(), 0, 0, ANARI_FLOAT32_VEC3, width, height);

      anari::setParameter(device, light, "radiance", radiance);
      anari::setParameter(device, light, "scale", l.scale);

      anariRelease(device, radiance);
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

} // namespace anari_viewer::windows
