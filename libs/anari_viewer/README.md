# libanari_viewer

This is a _source only_ library which lets downstream applications create
simple ANARI applications by reusing the components which implement the example
viewer in the SDK. While this library is intended to be useful, it is _not_
intended to be stable and may require continuous adjustment by projects which
consume it. This is ultimately for the purpose of demonstrating the primary
component of the ANARI-SDK: the [ANARI API](../anari/include/anari/anari.h).

### Using libanari_viewer

Using `libanari_viewer` is done via CMake by linking the target
`anari::anari_viewer`. This might look like:

```cmake
find_package(anari)
add_executable(myViewer)
# ...
target_link_libraries(myViewer anari::anari_viewer)
```

Note that this target is only installed if `INSTALL_VIEWER_LIBRARY` is enabled
when installing the SDK, which also requires enabling both `BUILD_EXAMPLES` and
`BUILD_VIEWER`. Behind the scenes, this library target is a pure `INTERFACE`
target that will import all the `.cpp` files necessary to use the viewer
components.

Given that this is not a stable library, it is assumed that users will read code
in order to understand how to use it. The provided [example
viewer](../../examples/viewer) application in the SDK is always a good place to
see how `libanari_viewer` is intended to be used.
