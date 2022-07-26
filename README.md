![logo](https://github.com/KhronosGroup/ANARI-Docs/blob/main/images/anari_RGB_Mar20.svg)

ANARI-SDK
=========

This repository contains the source for the ANARI API SDK. This includes:

- [Front-end library](libs/anari)
- [API utilties and helpers](libs/anari_utilities) (mostly for implementations)
- [Example device implementation](examples/example_device) (not for production use)
- [Example applications](examples/)
- [Interactive sample viewer](examples/viewer)
- [Unit tests](tests/unit)
- [Render tests](tests/render)

The 1.0 provisional ANARI specification can be found on the Khronos website
[here](https://www.khronos.org/registry/ANARI/).

Please note that the example device, sample applications, and tests do not yet
fully cover the entire specification and are still a work-in-progress. These
are expected to be completed in full on or before the official 1.0 ANARI
specification release.

The SDK is tested on Linux, but is also intended to be usable on other operating
systems such as macOS and Windows.

If you find any problems with the SDK, please do not hesitate to
[open an issue](https://github.com/KhronosGroup/ANARI-SDK/issues/new) on this
project!

## Building the SDK

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

## Using the SDK after install with CMake

The ANARI SDK exports CMake targets for the main front-end library and utilities
helper library. The targets which are exported are as follows:

- `anari::anari` : main library target to link with `libanari`
- `anari::anari_utilities` : library target which adds implementation helpers
- `anari::anari_library_debug` : library target for the debug device

These targets can be found with CMake via `find_package(anari)`. The examples
are written such that they can be built stand alone from the SDK source tree.
The simplest usage can be found [here](examples/simple).

## Running the examples

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
% ./anariViewer /path/to/some/file.obj
```

Alternatively, either `--library` or `-l` can be used to override the library to
be loaded on the command line directly.

The regression test binary used to render the test scenes without a window
(results saved out as PNG images) uses the same mechanisms as the viewer to
select/override which library is loaded at runtime.

## Available Implementations

### SDK provided ExampleDevice implementation

The [example device implementation](examples/example_device) is provided as a
starting point for users exploring the SDK and for implementors to see how the
API might be implemented. It uses OpenMP multi-threading for simplicity and is
not built to be a robust nor fast rendering engine. Users should look to use
vendor provided, hardware-optimized ANARI implementations which are shipped
independently from the SDK. (see below)

### List of publically available implementaions

Below is a list of available ANARI implemenations compatible with this SDK:

- [AMD RadeonProRender](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderANARI)
- [Intel OSPRay](https://github.com/ospray/anari-ospray)
- [NVIDIA USD](https://github.com/NVIDIA-Omniverse/AnariUsdDevice)
- [NVIDIA VisRTX](https://github.com/NVIDIA/VisRTX)

If you implement a backend to the ANARI SDK, please open a PR to add it to this
list!
