# CTS

The Conformance Test Suite (CTS) is a python toolkit to test the ANARI implementation of different libraries/devices against a reference device.
It contains the following features:
-  Render set of known test scenes
-  Image comparison “smoke tests”
-  Verification of object/parameter info metadata
-  Verification of known object properties
-  List core extensions implemented by a device
-  Create pdf report 

## Usage
### Requirements
The project was developed with the following python packages/versions. Other versions might work as well.
- Your ANARI library
- Python 3.10+
- numpy 1.23.0
- Pillow 9.3.0
- reportlab 3.6.10
- scikit_image 0.19.3
- sewar 0.4.5
- tabulate 0.8.10
  
If the cts binary (.pyd) file for the desired SDK version is not provided, have a look at the [Build section](#building).

Run the toolkit in the command line via
```
.\cts.py --help
```
This will show all available commands. To get more information about the individual commands call them with the help flag.

A detailed explanation of each feature can be found in [features.md]().


## Building
To build the CTS, CMake and a C++17 compiler is required. The project was tested with the MSVC compiler.\
On Windows the `ctsBackend` target does not show up in the CMake Tools for Visual Studio Code. It can either be seen in Visual Studio in the CMake Targets View or by using e.g. CMake GUI to create a Visual Studio solution. Nevertheless, the ctsBackend target it always build if build all is selected.

## Test scene format


## Extending the Scene Generator

## Creating the reference renderings/metadata

## Image compare functions

## Debugging
For easier development, build ANARI statically (by setting `BUILD_SHARED_LIBS=OFF`). The `CTS_DEV` option is enabled by default. This will copy the build binaries into the cts folder since they are needed for executing the python files. The helide and debug libraries are copied as well so they can be used as example devices.
`ctsBackend*.pyd` is the python module which is imported in the `cts.py` file.

To debug the python and C++ code simultaneously, the Visual Studio Code extension [Python C++ Debugger](https://marketplace.visualstudio.com/items?itemName=benjamin-simmonds.pythoncpp-debug) can be used. This automatically attaches the C++ debugger the correct process.

Here is a sample `launch.json` for Windows to get the debugger running:
```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Python C++ Debugger: CTS",
            "type": "pythoncpp",
            "request": "launch",
            "pythonLaunchName": "Python: CTS",
            "cppAttachName": "(Windows) Attach"
        },
        {
            "name": "Python C++ Debugger: CTS REF",
            "type": "pythoncpp",
            "request": "launch",
            "pythonLaunchName": "Python: CTS REF",
            "cppAttachName": "(Windows) Attach"
        },
        {
            "name": "(Windows) Attach",
            "type": "cppvsdbg",
            "request": "attach",
            "processId": ""
        },
        {
            "name": "Python: CTS",
            "type": "python",
            "request": "launch",
            "program": "${workspaceFolder}/cts.py",
            "cwd": "${workspaceFolder}",
            "console": "integratedTerminal",
            "env": {"PYTHONPATH": "${workspaceRoot}"},
            "args": ["render_scenes", "helide"]
        },
        {
            "name": "Python: CTS REF",
            "type": "python",
            "request": "launch",
            "program": "${workspaceFolder}/createReferenceData.py",
            "cwd": "${workspaceFolder}",
            "console": "integratedTerminal",
            "env": {"PYTHONPATH": "${workspaceRoot}"},
            "args": []
        }
    ]
}
```
Open this folder in VS Code. Add `/.vscode/launch.json` and run `Python C++ Debugger: CTS` to start debugging. Use `args` to invoke different python functions.\
 `Python C++ Debugger: CTS REF` can be used to debug `createReferenceData.py`.