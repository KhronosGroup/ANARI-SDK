#define ANARI_EXTENSION_UTILITY_IMPL

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
  m.def("query_features", &cts::queryExtensions, R"pbdoc(
        Query which extensions are supported by this device.
    )pbdoc", py::call_guard<py::gil_scoped_release>());
  m.def("query_metadata", &cts::queryInfo, R"pbdoc(
        Queries object/parameter info metadata of a library.
    )pbdoc", py::call_guard<py::gil_scoped_release>());

  m.def("getDefaultDeviceName", &cts::getDefaultDeviceName, py::call_guard<py::gil_scoped_release>());

  py::class_<cts::SceneGeneratorWrapper>(m, "SceneGenerator")
      .def(py::init<const std::string&, const std::optional<std::string>, const std::optional<py::function>&>(), py::call_guard<py::gil_scoped_release>())
      .def("setParameter", &cts::SceneGeneratorWrapper::setParam<std::string>, py::call_guard<py::gil_scoped_release>())
      .def("setParameter", &cts::SceneGeneratorWrapper::setParam<bool>, py::call_guard<py::gil_scoped_release>())
      .def("setParameter", &cts::SceneGeneratorWrapper::setParam<int>, py::call_guard<py::gil_scoped_release>())
      .def("setParameter", &cts::SceneGeneratorWrapper::setParam<float>, py::call_guard<py::gil_scoped_release>())
      .def("setParameter", &cts::SceneGeneratorWrapper::setParam<std::array<uint32_t, 3>>, py::call_guard<py::gil_scoped_release>())
      .def("createAnariObject", &cts::SceneGeneratorWrapper::createAnariObject, py::call_guard<py::gil_scoped_release>())
      .def("setCurrentObject", &cts::SceneGeneratorWrapper::setCurrentObject, py::call_guard<py::gil_scoped_release>())
      .def("setGenericParameter", &cts::SceneGeneratorWrapper::setGenericParameter<std::string>, py::call_guard<py::gil_scoped_release>())
      .def("setGenericParameter",&cts::SceneGeneratorWrapper::setGenericParameter<float>, py::call_guard<py::gil_scoped_release>())
      .def("setGenericParameter",&cts::SceneGeneratorWrapper::setGenericParameter<bool>, py::call_guard<py::gil_scoped_release>())
      .def("setGenericParameter",&cts::SceneGeneratorWrapper::setGenericParameter<std::array<float, 2>>, py::call_guard<py::gil_scoped_release>())
      .def("setGenericParameter",&cts::SceneGeneratorWrapper::setGenericParameter<std::array<float, 3>>, py::call_guard<py::gil_scoped_release>())
      .def("setGenericParameter",&cts::SceneGeneratorWrapper::setGenericParameter<std::array<float, 4>>, py::call_guard<py::gil_scoped_release>())
      //.def("setGenericParameter",&cts::SceneGeneratorWrapper::setGenericParameter<std::array<std::array<float, 3>, 4>>)
      .def("setGenericParameter",&cts::SceneGeneratorWrapper::setGenericParameter<std::array<std::array<float, 4>, 4>>, py::call_guard<py::gil_scoped_release>())
      .def("setGenericParameter",&cts::SceneGeneratorWrapper::setGenericParameter<std::array<std::array<float, 2>, 2>>, py::call_guard<py::gil_scoped_release>())
      .def("setGenericArray1DParameter",&cts::SceneGeneratorWrapper::setGenericArray1DParameter<std::vector<int>>, py::call_guard<py::gil_scoped_release>())
      .def("setGenericArray1DParameter",&cts::SceneGeneratorWrapper::setGenericArray1DParameter<std::vector<float>>, py::call_guard<py::gil_scoped_release>())
      .def("setGenericArray1DParameter",&cts::SceneGeneratorWrapper::setGenericArray1DParameter<std::vector<std::array<float, 2>>>, py::call_guard<py::gil_scoped_release>())
      .def("setGenericArray1DParameter",&cts::SceneGeneratorWrapper::setGenericArray1DParameter<std::vector<std::array<float, 3>>>, py::call_guard<py::gil_scoped_release>())
      .def("setGenericArray1DParameter",&cts::SceneGeneratorWrapper::setGenericArray1DParameter<std::vector<std::array<float, 4>>>, py::call_guard<py::gil_scoped_release>())
      .def("setGenericArray2DParameter",&cts::SceneGeneratorWrapper::setGenericArray2DParameter<std::vector<std::vector<int>>>, py::call_guard<py::gil_scoped_release>())
      .def("setGenericArray2DParameter",&cts::SceneGeneratorWrapper::setGenericArray2DParameter<std::vector<std::vector<float>>>, py::call_guard<py::gil_scoped_release>())
      .def("setGenericArray2DParameter",&cts::SceneGeneratorWrapper::setGenericArray2DParameter<std::vector<std::vector<std::array<float, 2>>>>, py::call_guard<py::gil_scoped_release>())
      .def("setGenericArray2DParameter",&cts::SceneGeneratorWrapper::setGenericArray2DParameter<std::vector<std::vector<std::array<float, 3>>>>, py::call_guard<py::gil_scoped_release>())
      .def("setGenericArray2DParameter",&cts::SceneGeneratorWrapper::setGenericArray2DParameter<std::vector<std::vector<std::array<float, 4>>>>, py::call_guard<py::gil_scoped_release>())
      .def("setGenericTexture2D",&cts::SceneGeneratorWrapper::setGenericTexture2D, py::call_guard<py::gil_scoped_release>())
      .def("setReferenceParameter",&cts::SceneGeneratorWrapper::setReferenceParameter, py::call_guard<py::gil_scoped_release>())
      .def("setReferenceParameter",&cts::SceneGeneratorWrapper::setReferenceArray, py::call_guard<py::gil_scoped_release>(), py::call_guard<py::gil_scoped_release>())
      .def("unsetGenericParameter", &cts::SceneGeneratorWrapper::unsetGenericParameter, py::call_guard<py::gil_scoped_release>())
      .def("releaseAnariObject", &cts::SceneGeneratorWrapper::releaseAnariObject, py::call_guard<py::gil_scoped_release>())
      .def("commit", &cts::SceneGeneratorWrapper::commit, py::call_guard<py::gil_scoped_release>())
      .def("renderScene", &cts::SceneGeneratorWrapper::renderScene, py::call_guard<py::gil_scoped_release>())
      .def(
          "resetAllParameters", &cts::SceneGeneratorWrapper::resetAllParameters, py::call_guard<py::gil_scoped_release>())
      .def("resetSceneObjects", &cts::SceneGeneratorWrapper::resetSceneObjects, py::call_guard<py::gil_scoped_release>())
      .def("getBounds", &cts::SceneGeneratorWrapper::getBounds, py::call_guard<py::gil_scoped_release>())
      .def("getFrameDuration", &cts::SceneGeneratorWrapper::getFrameDuration, py::call_guard<py::gil_scoped_release>());
}
