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

std::vector<std::tuple<std::string, bool>> queryFeatures(
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

  void commit()
  {
    if (m_sceneGenerator) {
      m_sceneGenerator->commit();
    }
  }

  void createAnariObject(const std::string &type, const std::string &subtype)
  {
    if (m_sceneGenerator) {
      m_sceneGenerator->createAnariObject(m_sceneGenerator->anariTypeFromString(type), subtype);
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

  std::vector<std::vector<uint32_t>> renderScene(
      const std::string& rendererType, float renderDistance)
  {
    if (m_sceneGenerator) {
      return m_sceneGenerator->renderScene(rendererType, renderDistance);
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

  void unsetGenericParameter(const std::string &name)
  {
    if (m_sceneGenerator) {
      m_sceneGenerator->unsetGenericParameter(name);
    }
  }
  
  void setReferenceParameter(const std::string& objectType, size_t objectIndex, const std::string& paramName, std::string& refType, size_t refIndex) {
    m_sceneGenerator->setReferenceParameter(
        m_sceneGenerator->anariTypeFromString(objectType), objectIndex, paramName,
        m_sceneGenerator->anariTypeFromString(refType), refIndex);
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
