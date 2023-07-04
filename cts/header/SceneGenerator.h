// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "scenes/scene.h"

#include <functional>
#include <optional>
#include <random>
#include <unordered_map>

namespace cts {

class SceneGenerator : public anari::scenes::TestScene
{
 public:
  SceneGenerator(anari::Device device);
  ~SceneGenerator();
  std::vector<anari::scenes::ParameterInfo> parameters() override;
  void resetAllParameters();
  void resetSceneObjects();
  
  void createAnariObject(
      int type, const std::string &subtype = "", const std::string& ctsType = "");
  template <typename T>
  void setGenericParameter(const std::string& name, T&& value)
  {
    if (m_device != nullptr && m_currentObject != nullptr) {
      anari::setParameter(m_device, m_currentObject, name.c_str(), value);
    }
  }

  template <typename T>
  void setGenericArray1DParameter(
      const std::string& name, const std::vector<T>& vector)
  {
    if (m_device != nullptr && m_currentObject != nullptr) {
      anari::setAndReleaseParameter(m_device, m_currentObject, name.c_str(), anari::newArray1D(m_device, vector.data(), vector.size()));
    }
  }

  void unsetGenericParameter(const std::string& name) {
    if (m_device != nullptr && m_currentObject != nullptr) {
      anari::unsetParameter(m_device, m_currentObject, name.c_str());
    }
  }

  void setReferenceParameter(int objType, size_t objIndex, const std::string& name, int refType, size_t refIndex);

  void setCurrentObject(int type, size_t index);

  void releaseAnariObject()
  {
    m_currentObject = nullptr;
  }

  anari::World world() override;

  void commit() override;

  std::vector<std::vector<uint32_t>> renderScene(float renderDistance);
  std::vector<std::vector<std::vector<std::vector<float>>>> getBounds();

  // after renderScene was called, use this to get the duration of the rendering
  float getFrameDuration() const
  {
    return frameDuration;
  }

  int anariTypeFromString(const std::string &input);

 private:

  float frameDuration = -1.0f;

  anari::World m_world{nullptr};
  std::unordered_map<int, std::vector<anari::Object>> m_anariObjects;
  anari::Object m_currentObject;
};

} // namespace cts
