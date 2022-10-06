# CTS

The Conformance Test Suite (CTS) is a python tool to test the ANARI implementation of different libraries/devices.
It contains the following features:
- [ ] Render set of known test scenes
- [ ] Image comparison “smoke tests”
- [ ] Verification of object/parameter info metadata
- [ ] Verification of known object properties
- [ ] List core extensions implemented by a device

The results can either be viewed in the command line or exported to a pdf file.

## Usage

## Development
For easier development, build ANARI statically (by setting `BUILD_SHARED_LIBS=OFF`). The `CTS_DEV` option is enabled by default. This will copy the build binaries, the example and debug libraries into the cts folder since they are needed for executing the python files.
`ctsBackend*.pyd` is the python module which is imported in the `cts.py` file.