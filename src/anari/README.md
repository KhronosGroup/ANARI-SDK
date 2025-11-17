# The ANARI Device Implementation Guide

This directory contains the ANARI frontend library (`libanari`) which is the
core C API which applications directly use. This document will outline the
details of what is necessary in order to _implement_ a device, as using the
API itself is detailed at length in the ANARI specification.

The primary function of this library is to dispatch
[ANARI C API function calls](include/anari/anari.h) to the corresponding ANARI
device implementing the API (designated by an `ANARIDevice` handle in the
function signature). There are two core concepts that implementations must
implement, along with optional additional tools which can aid in implementing
various ANARI features.

The headers used to implement the required components of a device for this
frontend library are found in the [backend directory](include/anari/backend) of
the core installed headers. Implementations are primarily expected to build
their devices against an installation of the ANARI-SDK.

## Implementing ANARILibrary

The first required item that must be implemented is `ANARILibrary`. This is done
by subclassing [`LibraryImpl`](include/anari/backend/LibraryImpl.h) and
providing the required method definitions. `LibraryImpl` acts as the concrete
representation of `ANARILibrary`, where the frontend library uses this class to
manage all C API calls which are done on `ANARILibrary` (where the function's
first argument is the library).

Note that `anariLoadLibrary()` uses the following naming convention for
dynamically loading an `ANARILibrary` at runtime: `anari_library_[name]`, where
`[name]` matches the string passed to `anariLoadLibrary`. `[name]` is used to
construct a C function name which is looked up as the entry point needed to
create an instance of `LibraryImpl`.  Implementations should use the
`ANARI_DEFINE_LIBRARY_ENTRYPOINT` macro at the bottom of `LibraryImpl.h` to
define this entrypoint function, where it is necessary to match the first macro
argument with `[name]` in the physical shared library file. For example, the
provided [`helide`](../devices/helide) device on linux compiles into the shared library
`libanari_library_helide.so`, and `helide` is the first argument to
`ANARI_DEFINE_LIBRARY_ENTRYPOINT`, as seen [here](../devices/helide/HelideLibrary.cpp).

It is possible to directly construct a device if client applications directly
links your library itself, but it is highly recommended to always provide the
dynamic path via `anariLoadLibrary()` and `LibraryImpl` as this is the most
common way to create ANARI devices. The alternate method of directly
constructing a device via linking is show by the
[`anariTutorialDirectLink`](../../examples/simple/anariTutorialDirectLink.c)
sample app which includes a custom header created by `helide` to directly
create an instance of the `helide` device.

## Implementing ANARIDevice

The majority of the ANARI C API is implemented by subclassing
[`DeviceImpl`](include/anari/backend/DeviceImpl.h). Similar to implementing
`ANARILibrary`, `ANARIDevice` directly represents an instance of `DeviceImpl`
where the methods of the class correspond to functions in the ANARI API handled
by the device (where the function's first argument is the device). Device
implementations should always seek to make each instance of `DeviceImpl`
independent from each other by avoiding any shared state between them (i.e.
static state within the class or shared library).

Almost the entirety of `DeviceImpl` directly corresponds to functions found
in [`anari.h`](include/anari/anari.h). The only state held by `DeviceImpl` are
the default status callback and callback user pointer. `DeviceImpl` is intended
to be very minimal -- implementors who desire SDK-provided implementations of
much of the API should use the [`helium`](../helium) layer which implements many
common concepts, but requires implementations to opt-in to various `helium`
abstractions and classes. The provided [`helide`](../helide) device demonstrates
ultimately how to implement `DeviceImpl` through using
[`helium::BaseDevice`](../helium/BaseDevice.h).  Device implementors can use the
sum of `helium::BaseDevice` and
[`helide::HelideDevice`](../helide/HelideDevice.h) as a full example of
implementing ANARI. Further documentation of what functionality `helium`
provides can be found in its [README](../helium/README.md).

## Object query code generation

Devices should implement the device and object queries offered by the ANARI API
in order for applications to introspect what extensions (and their details) are
offered. However, these functions can be quite tedius and repetitive to
implement, so Python-based code generation tools are offered to let library and
device authors minimize the effort in writing them. They use JSON definition
files to generate C++ which can then be used to implement the various query
functions.

All of the Python tooling is installed when `INSTALL_CODE_GEN_SCRIPTS` is
enabled when the SDK is installed. This can then be consumed by including the
`code_gen` component name in find package:

```cmake
find_package(anari REQUIRED COMPONENTS code_gen)
```

This brings the `anari_generate_queries()` CMake function into scope downstream,
which has the following signature:

```cmake
anari_generate_queries(
  NAME [device_name]
  PREFIX [device_prefix]
  CPP_NAMESPACE [namespace]
  JSON_DEFINITIONS_FILE [path/to/device/definitions.json]
)
```

This CMake function will create a target called `generate_[device_name]_device`,
which must be built manually in order to generate any C++. By invoking this
targets, a `[device_prefix]Queries.cpp/.h` are created, which can be included
in the local device build's source. Please refer to [helide](../helide) as an
example of how these components all go together.

Note that all core spec extensions are defined in a collection of
[JSON files](../../code_gen/api) that are referenced in the downstream JSON
definitions. It is recommended to copy an existing JSON definitions file and
modify it accordingly.
