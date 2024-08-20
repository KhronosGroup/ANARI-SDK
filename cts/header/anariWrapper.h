#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <memory>

#include <pybind11/pybind11.h>

#include <anari/anari_cpp.hpp>
#include "SceneGenerator.h"

namespace cts {
void statusFunc(const void *userData,
    ANARIDevice device,
    ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message);

std::string getDefaultDeviceName(const std::string &libraryName,
    const std::optional<pybind11::function>
        &callback);

std::vector<std::tuple<std::string, bool>> queryExtensions(
    const std::string &libraryName,
    const std::optional<std::string> &device,
    const std::optional<pybind11::function>
        &callback);

// SceneGeneratorWrapper wraps the SceneGenerator functionality
// to ensure the python callback is not garbage collected by pybind11
class SceneGeneratorWrapper
{
 public:
  SceneGeneratorWrapper(const std::string &library,
      const std::optional<std::string> &device,
      const std::optional<pybind11::function> &callback);
  ~SceneGeneratorWrapper();

  void loadGLTF(const std::string &jsonText,
      std::vector<std::string> &sortedBuffers,
      std::vector<std::string> &sortedImages)
  {
    if (m_sceneGenerator) {
      std::vector<std::vector<char>> convertedBuffers;
      std::vector<std::vector<char>> convertedImages;
      for (size_t i = 0; i < sortedBuffers.size(); ++i) {
        convertedBuffers.emplace_back(sortedBuffers[i].begin(),
            sortedBuffers[i].end());
      }
      for (size_t i = 0; i < sortedImages.size(); ++i) {
        convertedImages.emplace_back(
            sortedImages[i].begin(), sortedImages[i].end());
      }
      m_sceneGenerator->loadGLTF(jsonText, convertedBuffers, convertedImages);
    }
  }

  void commit()
  {
    if (m_sceneGenerator) {
      m_sceneGenerator->commit();
    }
  }

  void createAnariObject(const std::string &type,
      const std::string &subtype = "",
      const std::string& ctsType = "")
  {
    if (m_sceneGenerator) {
      m_sceneGenerator->createAnariObject(
          m_sceneGenerator->anariTypeFromString(type), subtype, ctsType);
    }
  }

  void releaseAnariObject()
  {
    if (m_sceneGenerator) {
      m_sceneGenerator->releaseAnariObject();
    }
  }

  void resetSceneObjects()
  {
    if (m_sceneGenerator) {
      m_sceneGenerator->resetSceneObjects();
    }
  }

  void resetAllParameters()
  {
    if (m_sceneGenerator) {
      m_sceneGenerator->resetAllParameters();
    }
  }

  std::vector<std::vector<uint32_t>> renderScene(float renderDistance)
  {
    if (m_sceneGenerator) {
      return m_sceneGenerator->renderScene(renderDistance);
    }
    return {};
  }

  std::vector<std::vector<std::vector<std::vector<float>>>> getBounds() {
    if (m_sceneGenerator) {
      return m_sceneGenerator->getBounds();
    }
    return {};
  }

  float getFrameDuration() const {
    if (m_sceneGenerator) {
      return m_sceneGenerator->getFrameDuration();
    }
    return -1.0f;
  }

  template <typename T>
  void setParam(const std::string& name, const T& t)
  {
    if (m_sceneGenerator) {
      m_sceneGenerator->setParam(name, t);
    }
  }

  template <typename T>
  void setGenericParameter(const std::string &name, T &&t)
  {
    if (m_sceneGenerator) {
      m_sceneGenerator->setGenericParameter(name, t);
    }
  }

  template <typename T>
  void setGenericArray1DParameter(const std::string &name, T &t)
  {
    if (m_sceneGenerator) {
      m_sceneGenerator->setGenericArray1DParameter(name, t);
    }
  }

  template <typename T>
  void setGenericArray2DParameter(const std::string &name, T &t)
  {
    if (m_sceneGenerator) {
      m_sceneGenerator->setGenericArray2DParameter(name, t);
    }
  }

  void setGenericTexture2D(
      const std::string& name, const std::string& textureType)
  {
    if (m_sceneGenerator) {
      m_sceneGenerator->setGenericTexture2D(name, textureType);
    }
  }

  void unsetGenericParameter(const std::string &name)
  {
    if (m_sceneGenerator) {
      m_sceneGenerator->unsetGenericParameter(name);
    }
  }
  
  void setReferenceParameter(const std::string& objectType, size_t objectIndex, const std::string& paramName, const std::string& refType, size_t refIndex) {
    m_sceneGenerator->setReferenceParameter(
        m_sceneGenerator->anariTypeFromString(objectType), objectIndex, paramName,
        m_sceneGenerator->anariTypeFromString(refType), refIndex);
  }
  void setReferenceArray(const std::string &objectType,
      size_t objectIndex,
      const std::string &paramName,
      const std::string &refType,
      const std::vector<size_t>& refIndices)
  {
    m_sceneGenerator->setReferenceArray(
        m_sceneGenerator->anariTypeFromString(objectType),
        objectIndex,
        paramName,
        m_sceneGenerator->anariTypeFromString(refType),
        refIndices);
  }

  void setCurrentObject(std::string &type, size_t index)
  {
    m_sceneGenerator->setCurrentObject(m_sceneGenerator->anariTypeFromString(type), index);
  }

 private:
  std::unique_ptr<SceneGenerator> m_sceneGenerator;
  anari::Library m_library;
  std::optional<pybind11::function> m_callback;
};

} // namespace cts
