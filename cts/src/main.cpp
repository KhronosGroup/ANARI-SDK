#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <anariWrapper.h>

namespace py = pybind11;

PYBIND11_MODULE(cts, m)
{
  m.def("render_scenes", &cts::renderScenes, R"pbdoc(
        Creates renderings of specified scenes.
    )pbdoc");
}
