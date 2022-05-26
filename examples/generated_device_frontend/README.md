# Frontend Generator Example

This example demonstrates infrastructure provided for auto-generating the
frontend of a device implementation. This is implemented as python and is found
under the `code_gen` directory, but is usable entirely from CMake.

The CMake function which orchestrates code generation is
`anari_generate_frontend()` and is demonstrated in this directory's
CMakeLists.txt. This function is usable immediately after projects do
`find_package(anari)` in their own CMake code.

The following arguments are required to pass to `anari_generate_frontend()`:

- `TARGET [name]` -- name of the custom CMake target which generates the code
- `PREFIX [name]` -- name of the C++ `Device` subclass name (`[prefix]Device`)
- `NAMESPACE [name]` -- namespace for the implementation
- `DESTINATION [path]` -- place where the generated code will be put

The following arguments are also available, but are optional:

- `CODE_HEADER_FILE [file]` -- file header text, usually copyright comment
- `DEFINITIONS [json_file]` -- file containing what subtypes should be
  generated: when this option is omitted, `devices/extended_device.json` is used

Code generation will occur automatically at CMake time if `DESTINATION` does not
exist. This lets `add_subdirectory()` of the generated code be immediately
available. The code generation custom command target, though, can be explicitly
invoked at build time by explicitly building the target (it is not included
in `ALL`).

Note that some generated files are expected to be built upon (`src/`) and others
are expected to not ever be changed (`generated/`). Generated source files have
comments just below the injected header text to indicate if the file should or
should not be edited directly.
