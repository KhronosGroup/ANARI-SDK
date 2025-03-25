// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari_test_scenes/scenes/file/gltf2anari.h"
#include "anari_test_scenes_export.h"
#include "scenes/scene.h"

#include <functional>
#include <optional>
#include <random>
#include <unordered_map>

namespace anari {
namespace scenes {
class SceneGenerator : public anari::scenes::TestScene
{
 public:
  ANARI_TEST_SCENES_INTERFACE SceneGenerator(anari::Device device);
  ANARI_TEST_SCENES_INTERFACE ~SceneGenerator();
  ANARI_TEST_SCENES_INTERFACE std::vector<anari::scenes::ParameterInfo>
  parameters() override;
  ANARI_TEST_SCENES_INTERFACE void resetAllParameters();
  ANARI_TEST_SCENES_INTERFACE void resetSceneObjects();

  ANARI_TEST_SCENES_INTERFACE void loadGLTF(const std::string &jsonText,
      std::vector<std::vector<char>> &sortedBuffers,
      std::vector<std::vector<char>> &sortedImages,
      bool generateTangents,
      bool parseLights);

  ANARI_TEST_SCENES_INTERFACE void createAnariObject(int type,
      const std::string &subtype = "",
      const std::string &ctsType = "");
  template <typename T>
  void setGenericParameter(
      const std::string &name, T &&value)
  {
    if (m_device != nullptr && m_currentObject != nullptr) {
      anari::setParameter(m_device, m_currentObject, name.c_str(), value);
    }
  }

  template <typename T>
  void setGenericArray1DParameter(
      const std::string &name, const std::vector<T> &vector)
  {
    if (m_device != nullptr && m_currentObject != nullptr) {
      anari::setAndReleaseParameter(m_device,
          m_currentObject,
          name.c_str(),
          anari::newArray1D(m_device, vector.data(), vector.size()));
    }
  }

  template <typename T>
  void setGenericArray2DParameter(
      const std::string &name, const std::vector<std::vector<T>> &vector)
  {
    if (vector.empty() || vector.front().empty()) {
      return;
    }
    if (m_device != nullptr && m_currentObject != nullptr) {
      std::size_t total_size = 0;
      for (const auto &subvector : vector) {
        total_size += subvector.size();
      }
      std::vector<T> result;
      result.reserve(total_size);
      for (const auto &subvector : vector) {
        result.insert(result.end(), subvector.begin(), subvector.end());
      }

      anari::setAndReleaseParameter(m_device,
          m_currentObject,
          name.c_str(),
          anari::newArray2D(
              m_device, result.data(), vector.size(), vector.front().size()));
    }
  }

  ANARI_TEST_SCENES_INTERFACE void setGenericTexture2D(
      const std::string &name, const std::string &textureType);

  ANARI_TEST_SCENES_INTERFACE void unsetGenericParameter(
      const std::string &name)
  {
    if (m_device != nullptr && m_currentObject != nullptr) {
      anari::unsetParameter(m_device, m_currentObject, name.c_str());
    }
  }

  ANARI_TEST_SCENES_INTERFACE void setReferenceParameter(int objType,
      size_t objIndex,
      const std::string &name,
      int refType,
      size_t refIndex);
  ANARI_TEST_SCENES_INTERFACE void setReferenceArray(int objType,
      size_t objIndex,
      const std::string &name,
      int refType,
      const std::vector<size_t> &refIndices);

  ANARI_TEST_SCENES_INTERFACE void setCurrentObject(int type, size_t index);

  ANARI_TEST_SCENES_INTERFACE void releaseAnariObject()
  {
    m_currentObject = nullptr;
  }

  ANARI_TEST_SCENES_INTERFACE anari::World world() override;

  ANARI_TEST_SCENES_INTERFACE void commit() override;

  ANARI_TEST_SCENES_INTERFACE
      std::tuple<std::vector<std::vector<uint32_t>>, uint32_t, uint32_t>
      renderScene(float renderDistance);
  ANARI_TEST_SCENES_INTERFACE
      std::vector<std::vector<std::vector<std::vector<float>>>>
      getBounds();

  // after renderScene was called, use this to get the duration of the rendering
  ANARI_TEST_SCENES_INTERFACE float getFrameDuration() const
  {
    return frameDuration;
  }

  ANARI_TEST_SCENES_INTERFACE int anariTypeFromString(const std::string &input);

 private:
  float frameDuration = -1.0f;

  gltf_data m_gltf;
  anari::Frame m_frame{nullptr};
  anari::World m_world{nullptr};
  std::unordered_map<int, std::vector<anari::Object>> m_anariObjects;
  anari::Object m_currentObject = nullptr;
};
} // namespace scenes
} // namespace anari
