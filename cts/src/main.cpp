#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include <anariWrapper.h>

namespace py = pybind11;

PYBIND11_MODULE(ctsBackend, m)
{
  m.def("render_scenes", &cts::renderScenes, R"pbdoc(
        Creates renderings of specified scenes.
    )pbdoc");
  m.def("check_core_extensions", &cts::checkCoreExtensions, R"pbdoc(
        Check which core extensions are supported by a device.
    )pbdoc");
}
