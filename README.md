![logo](https://github.com/KhronosGroup/ANARI-Docs/blob/main/images/anari_RGB_Mar20.svg)

ANARI-SDK
=========

This repository contains the source for the ANARI API SDK. This includes:

- [Front-end library](libs/anari)
- [API utilties and helpers](libs/anari_utilities) (mostly for implementations)
- [Example device implementation](examples/example_device)
- [Example applications](examples/)
- [Interactive sample viewer](examples/viewer)
- [Unit tests](tests/unit)
- [Regression tests](tests/regression)

### Building the SDK

The repository uses CMake 3.11+ to build the library, example implementation,
sample apps, and tests. For example, to build (must be in a separate directory
from the source directory), you can do:

```bash
% cd /path/to/anari
% mkdir build
% cd build
% cmake ..
```

Using a tool like `ccmake` or `cmake-gui` will let you see which options are
available to enable. The following CMake options are offered:

- `BUILD_SHARED_LIBS` : build everything as shared libraries or static libraries
- `BUILD_TESTING`     : build unit and regression test binaries
- `BUILD_EXAMPLES`    : build example device and example applications
- `BUILD_VIEWER`      : build viewer too (needs glfw3) if building examples

Once built, the library can be installed via the `install` target created by
CMake. This can be invoked from your build directory with (on any platform):

```bash
% cmake --build . -t install
```

### Using the SDK after install with CMake

The ANARI SDK exports CMake targets for the main front-end library and utilities
helper library. The targets which are exported are as follows:

- `anari::anari` : main library target to link with `libanari`
- `anari::anari_utilities` : library target which adds implementation helpers

These targets can be found with CMake via `find_package(anari)`. There is an
example provided to demonstrate what consuming the ANARI SDK with CMake looks
like [here](examples/cmake_find_anari).

### Running the examples

The basic tutorial app (built by default) uses the `example` device as an
example, which can be run with:

```bash
% ./anariTutorial
```

The viewer application (enabled with `BUILD_VIEWER=ON`) by default uses the
`environment` library, which reads `ANARI_LIBRARY` as an environment variable to
get the library to load. For example it can be run with:

```bash
% export ANARI_LIBRARY=example
% ./viewer /path/to/some/file.obj
```

Alternatively, either `--library` or `-l` can be used to override the library to
be loaded on the command line directly.

The regression test binary used to render the test scenes without a window
(results saved out as PNG images) uses the same mechanisms as the viewer to
select/override which library is loaded at runtime.
