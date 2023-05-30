#define ANARI_FEATURE_UTILITY_IMPL

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include "anariWrapper.h"
#include "anariInfo.h"

#include <functional>
#include <string>

namespace py = pybind11;

PYBIND11_MODULE(anariCTSBackend, m)
{
  m.def("query_features", &cts::queryFeatures, R"pbdoc(
        Query which features are supported by this device.
    )pbdoc");
  m.def("query_metadata", &cts::queryInfo, R"pbdoc(
        Queries object/parameter info metadata of a library.
    )pbdoc");

  m.def("getDefaultDeviceName", &cts::getDefaultDeviceName);

  py::class_<cts::SceneGeneratorWrapper>(m, "SceneGenerator")
      .def(py::init<const std::string&, const std::optional<std::string>, const std::optional<py::function>&>())
      .def("setParameter", &cts::SceneGeneratorWrapper::setParam<std::string>)
      .def("setParameter", &cts::SceneGeneratorWrapper::setParam<int>)
      .def("createAnariObject", &cts::SceneGeneratorWrapper::createAnariObject)
      .def("setGenericParameter", &cts::SceneGeneratorWrapper::setGenericParameter<std::string>)
      .def("setGenericParameter",&cts::SceneGeneratorWrapper::setGenericParameter<float>)
      .def("setGenericParameter",&cts::SceneGeneratorWrapper::setGenericParameter<std::array<float, 3>>)
      .def("releaseAnariObject", &cts::SceneGeneratorWrapper::releaseAnariObject)
      .def("commit", &cts::SceneGeneratorWrapper::commit)
      .def("renderScene", &cts::SceneGeneratorWrapper::renderScene)
      .def(
          "resetAllParameters", &cts::SceneGeneratorWrapper::resetAllParameters)
      .def("resetSceneObjects", &cts::SceneGeneratorWrapper::resetSceneObjects)
      .def("getBounds", &cts::SceneGeneratorWrapper::getBounds)
      .def("getFrameDuration", &cts::SceneGeneratorWrapper::getFrameDuration);
}
