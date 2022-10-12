#define ANARI_FEATURE_UTILITY_IMPL

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include "anariWrapper.h"
#include "anariInfo.h"
#include "SceneGenerator.h"

#include <functional>
#include <string>

namespace py = pybind11;

PYBIND11_MODULE(ctsBackend, m)
{
  m.def("render_scenes", &cts::renderScenes, R"pbdoc(
        Creates renderings of specified scenes.
    )pbdoc");
  m.def("check_core_extensions", &cts::checkCoreExtensions, R"pbdoc(
        Check which core extensions are supported by a device.
    )pbdoc");
  m.def("query_metadata", &cts::queryInfo, R"pbdoc(
        Queries object/parameter info metadata of a library.
    )pbdoc");

  py::class_<cts::SceneGenerator>(m, "SceneGenerator")
      .def(py::init<
          const std::string &,
          const std::string &,
          const std::function<void(const std::string message)>&
      >())
      .def("setParameter", &cts::SceneGenerator::setParamDirect)
      .def("commit", &cts::SceneGenerator::commit)
      .def("renderScene", &cts::SceneGenerator::renderScene);
}
