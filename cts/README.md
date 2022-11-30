# CTS

The Conformance Test Suite (CTS) is a python toolkit to test the ANARI implementation of different libraries/devices against a reference device.
It contains the following features:
-  Render set of known test scenes
-  Image comparison “smoke tests”
-  Verification of object/parameter info metadata
-  Verification of known object properties
-  List core extensions implemented by a device
-  Create pdf report 

## Requirements

The project was developed with the following python packages/versions. Other versions might work as well.
- Your ANARI library files copied into this folder
- Python 3.10+
- Pillow 9.3.0
- reportlab 3.6.10
- scikit_image 0.19.3
- tabulate 0.8.10
  
If the cts binary (.pyd) file for the desired SDK version is not provided, have a look at the [Build section](#building).

## Usage

Run the toolkit in the command line via
```
.\cts.py --help
```
This will show all available commands. To get more information about the individual commands call them with the help flag.

A detailed explanation of each feature can be found in [features.md](features.md).

Example: To create the pdf report for the helide library, make sure the `anari_library_helide.dll` (or *.so / *.dylib) is placed into this folder and call:
```
.\cts.py create_report helide
```

Not all C++ exceptions from ANARI devices can be caught. If possible the exception will be printed on the console and the task resumes (e.g. with the next test) if possible. If the CTS crashes, have a look at the `ANARI.log` file. It contains all ANARI logger information and might reveal the reason of a crash or unexpected behavior.

## Building
To build the CTS, CMake and a C++17 compiler is required. The project was tested with the MSVC compiler.\
On Windows the `ctsBackend` target does not show up in the CMake Tools for Visual Studio Code. It can either be seen in Visual Studio in the CMake Targets View or by using e.g. CMake GUI to create a Visual Studio solution. Nevertheless, the ctsBackend target is always build when `build all` is selected.

For easier development, build ANARI statically (by setting `BUILD_SHARED_LIBS=OFF`). The `CTS_DEV` option is enabled by default. This will copy the build binaries into the `cts` folder since they are needed for executing the python files. The helide and debug libraries are copied as well so they can be used as example devices.
`ctsBackend*.pyd` is the python module which is imported in the `cts.py` file.

## Test scene format
A test is written as a JSON file with a specific structure. [`default_test_scene.json`](default_test_scene.json) contains the default values for each test. These can be overwritten by the specific tests. Tests should be placed in the [test_scenes folder](test_scenes/). By using subfolders, categories can be created that allow running the CTS on selected tests only.

Here is the [sphere test file](test_scenes/primitives/sphere/sphere.json) as an example:

```json
{
  "sceneParameters": {
    "geometrySubtype": "sphere",
    "seed": 54321
  },
  "permutations": {
    "primitiveCount": [
      1,
      16
    ]
  },
  "variants": {
    "primitiveMode": [
      "soup",
      "indexed"
    ]
  },
  "requiredFeatures": [
    "ANARI_KHR_GEOMETRY_SPHERE"
  ],
  "metaData": {
    "1": {
      "bounds": {
        "world": [
          [
            0.8382989764213562,
            0.43637922406196594,
            0.5504830479621887
          ],
          [
            0.9849825501441956,
            0.5830627679824829,
            0.6971666216850281
          ]
        ]
      }
    },
    "16": {
      "bounds": {
        "world": [
          [
            -0.08789964765310287,
            -0.28438711166381836,
            -0.20388446748256683
          ],
          [
            1.1669179201126099,
            1.1838836669921875,
            1.3429962396621704
          ]
        ]
      }
    }
  }
}
```
- `sceneParameters`: This json objects contains all key value pairs which are passed to the C++ scene generator. The following parameters are supported. They are also documented in the [parameters() function in SceneGenerator.cpp](src/SceneGenerator.cpp/#L38):
  -  `geometrySubtype`: Which type of geometry to generate. Possible values: `triangle`, `quad`, `sphere`, `curve`, `cone`, `cylinder`. Default: `triangle`
  -  `shape`: Which shape should be generated. Currently only relevant for triangles and quads. Possible values: `triangle`, `quad`, `cube`. Default: `triangle`
  -  `primitiveMode`: How the vertex data is arranged (`soup` or `indexed`). Default: `soup`
  -  `primitiveCount`: Number of primitives to generate. Default: `1`
  -  `frame_color_type`: Type of the color framebuffer. If empty, color buffer will not be used. Possible values: `UFIXED8_RGBA_SRGB`, `FLOAT32_VEC4`, `UFIXED8_VEC4`
  -  `frame_depth_type`: Type of the depth framebuffer. If empty, depth buffer will not be used. Possible values: `FLOAT32`
  -  `image_height`: Height of the image. Default: `1024`
  -  `image_width`: Width of the image. Default: `1024`
  -  `attribute_min`: Minimum random value for attributes. Default: `0.0`
  -  `attribute_max`: Maximum random value for attributes. Default: `1.0`
  -  `primitive_attributes`: If primitive attributes should be filled randomly. Default: `true`
  -  `vertex_attribtues`: If vertex attributes should be filled randomly. Default: `true`
  -  `seed`: Seed for random number generator to ensure that tests are consistent across platforms. Default `0`
-  `permutations` and `variants`: These specify scene parameters where different values should be tested. Therefore, these JSON objects contain the name of a scene parameter and a list of values for each of them. The permutations specify changes which result in a different outcome (e.g. 1 or 16 primitives). The variants should not change the outcome, therefore only one reference rendering is needed for these (e.g. the soup and indexed variants should look the same). The cartesian product is performed for all parameters. In the current example this results in four test images: 1 primitive + soup, 1 primitive + indexed, 16 primitives + soup, 16 primitives + indexed; As well as two reference images: 1 primitive (soup or indexed), 16 primitives (soup or indexed).
-  `requiredFeatures`: A list of features which need to be supported by the device to perform the test. Some features (e.g. perspective camera) which are needed for every test are implicitly required. This list should only contain the features which are explicitly tested. If the device does not support the feature, the test is skipped.
-  `metaData`: This JSON object is automatically created while generating the reference images. It contains ANARI object properties for each permutation (currently only bounds information).

## Extending the Scene Generator

The C++ code is divided into the following source file:
- `main.cpp` contains the pybind11 definitions. For more information check the pybind11 documentation
- `anariInfo.cpp` contains the refactored version of the `anariInfo.cpp` from the examples
- `ctsQueries.cpp` is autogenerated by the generate_cts target
- `anariWrapper.cpp` contains the functions which do not need to be applied on all scenes (query_features), the ANARI status function, and a wrapper around the SceneGenerator. This wrapper is needed to ensure the python callback is not garbage collected by pybind11
- `SceneGenerator.cpp` inherits the `TestScene` class from `anari_test_scenes` and translates the test cases into ANARI objects
- `PrimitiveGenerator.cpp` is used by the `SceneGenerator` to create the data needed for the ANARI geometries (e.g. vertex data, radii)



## Creating the reference renderings/metadata
The `generateCTS` target can be build to generate all reference images, add the meta data to the test JSON files and generate the C++ ANARI query code. Currently the `helide` library is used to generate the references. To change this, modify `REFERENCE_LIB` in [CMakeLists.txt](CMakeLists.txt/#L10). One can also manually call the [`createReferenceData.py`](createReferenceData.py) script.\
This will clear and set the metaData objects of all tests in [`test_scenes`](test_scenes/) and place the reference renderings next to the corresponding json files with a `ref_` prefix.

## Extending the python scripts
The python code is divided into 4 files:
- [cts.py](cts.py): The main python file which parses all arguments and contains functions for each feature
- [ctsUtility.py](ctsUtility.py): This file contains utility functions e.g. image comparison functions
- [ctsReport.py](ctsReport.py): This file contains the functions to create the PDF report from JSON style input data
- [createReferenceData.py](createReferenceData.py): This script can be used to create the reference data

### Image compare functions
The comparison functions are defined at the top of [ctsUtility.py](ctsUtility.py). Currently, `PSMR` and `SSIM` from `scikit_image` are used as metrics. To permanently add a new metric, define a function like this:
```python
def newMetric(reference, candidate, threshold):
    result = newMetric(reference, candidate)
    return result > threshold, threshold, result
```
The function takes the reference and candidate pixel data and a threshold. It returns a bool if the candidate passed the test, the threshold and the actual result.

Now this function needs to be called in `evalute_metrics`:
```python
if "newMetric" in methods:
    threshold =  getThreshold(methods, thresholds, "newMetric", 0.7) # 0.7 is the default threshold value
    passed["newMetric"], usedThresholds["newMetric"], results["newMetric"] = newMetric(reference, candidate, threshold)
```
Now the new metric can be used and is automatically added to the report.

Another way to add a new metric temporarily is by using the `custom_compare_function` parameter of `compare_images` or `create_report`. This parameter can be set to a function with the following  signature:
```python
def custom_compare_function(reference, candidate):
    # ...    
    return result > threshold, threshold, result
```
Note that no threshold is passed to the custom function. The threshold needs to be defined in the custom function and be returned as the second value.

### Apply to scene
To add new functionality to the CTS the `apply_to_scene` function can be used.
```python
def function_per_scene(parsed_json, sceneGenerator, anari_renderer, scene_location, test_name, permutationString, variantString)

def apply_to_scenes(func, anari_library, anari_device = None, anari_renderer = "default", test_scenes = "test_scenes", only_permutations = False, use_generator = True,  *args)
```
It takes a function e.g. `function_per_scene` and calls it for each scene or each scene permutation, if present. The function receives the parsed json test file, the C++ sceneGenerator object, the name of the ANARI renderer, the file location of the test, the name of the test, the current permutation and variant or empty strings if none exist.
The return value is stored in a dictionary as a value with the key being the test's name + the permutation/variant string.

The `only_permutations` parameter of `apply_to_scenes` can be used to ignore variants. This is relevant for the reference creation. `use_generator` specifies if the C++ SceneGenerator should be initialized or not. In the latter case, `sceneGenerator` is set to `None`. Additional parameters can added (denoted as `*args`) and will be passed directly to the apply function. 

### PDF report
The PDF report is created in [ctsReport.py](ctsReport.py) with the reportlab library. It takes a JSON-like structured item, which represents the different sections and subsection of the PDF. Therefore, the required features for each test can be found in `data[test_name]`, while the actual result can be found in `data[test_name][permutation][channel]`. Test case independent information such as the supported features and anariInfo output is stored in the top level JSON data. Additionally, a summary is compiled at the top of the report, showing each test case and whether it failed or passed, as well as a link to the corresponding detailed page. By default the detailed pages are not created. `--verbose_error` (verbosity level 1) shows these pages for failed tests and `--verbose_all` (verbosity level 2) shows it for every test regardless of its outcome.

## Debugging
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
Open the `cts` folder in VS Code. Add `/.vscode/launch.json` and run `Python C++ Debugger: CTS` to start debugging. Use `args` to invoke different python functions.\
 `Python C++ Debugger: CTS REF` can be used to debug `createReferenceData.py`.