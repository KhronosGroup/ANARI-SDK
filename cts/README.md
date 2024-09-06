# CTS

The Conformance Test Suite (CTS) is a python toolkit using [pybind11](https://github.com/pybind/pybind11) to test the ANARI implementation of different libraries/devices against a reference device.
It contains the following features:

- Render set of known test scenes
- Image comparison “smoke tests”
- Verification of object/parameter info metadata
- Verification of known object properties
- List core extensions implemented by a device
- Create pdf report

## Requirements

The project uses poetry to manage package versions. If using poetry is not wanted, one can manually install the dependencies specified in [pyproject.toml](pyproject.toml).

- Your ANARI library files copied into this folder
- [Python 3.12](https://www.python.org/downloads/)
- [poetry](https://python-poetry.org/)

Install poetry as described [here](https://python-poetry.org/docs/#installing-with-pipx). You might need to add the poetry executable to your PATH.
In the cts folder, run `poetry install` to install all dependencies and afterwards `poetry shell` to enter the virtual environment. From this shell you can run the cts normally via `python cts.py`.

If the cts binary (.pyd) file for the desired SDK version is not provided, have a look at the [Build section](#building).

## Install
CTS will be installed alongside ANARI via calling
```bash
% cmake --build . -t install
```
in the main ANARI build folder.
To run the CTS, one has to set the `PYTHONPATH` environment variable to the location of the ANARI library files.
On Linux this defaults to `/usr/local/lib`, on Windows to `<install location>/bin`.
The `cts.py` file as well as all test scenes etc. are located in `<install location>/share/anari/cts`. 
## Usage

Run the toolkit in the command line via

```
.\cts.py --help
```
On linux one might need to make `cts.py` executable by running `chmod +x cts.py`.

This will show all available commands. To get more information about the individual commands call them with the help flag.

A detailed explanation of each feature can be found in [features.md](features.md).

Example: To create the pdf report for the helide library, make sure the `anari_library_helide.dll` (or _.so / _.dylib) is placed into this folder and call:

```
.\cts.py create_report helide
```

Not all C++ exceptions from ANARI devices can be caught. If possible, the exception will be printed to the console and the task will resume (e.g. continuing with the next test). If the CTS crashes, have a look at the `ANARI.log` file. It contains all ANARI logger information and might reveal the reason of a crash or unexpected behavior. It is found by default in the current working directory, but the location can also be specified using `--log_dir` or the environment variable `ANARI_CTS_LOG`.

## Building

To build the CTS, CMake and a C++17 compiler is required. The project was tested with the MSVC compiler.\
On Windows the `anariCTSBackend` target does not show up in the CMake Tools for Visual Studio Code. It can either be seen in Visual Studio in the CMake Targets View or by using e.g. CMake GUI to create a Visual Studio solution. Nevertheless, the anariCTSBackend target is always build when `build all` is selected.

For easier development, build ANARI statically (by setting `BUILD_SHARED_LIBS=OFF`). The `CTS_DEV` option will copy the build binaries into the `cts` folder since they are needed for executing the python files. The helide and debug libraries are copied as well so they can be used as example devices.
`anariCTSBackend*.pyd` is the python module which is imported in the `cts.py` file.

Once built, the library can be installed via the `install` target created by
CMake. This is invoked for the whole ANARI project from your build directory with (on any platform):

```bash
% cmake --build . -t install
```

## Test scene format

A test is written as a JSON file with a specific structure. [`default_test_scene.json`](default_test_scene.json) contains the default values for each test. These can be overwritten by the specific tests. Tests should be placed in the [test_scenes folder](test_scenes/). By using subfolders, categories can be created that allow running the CTS on selected tests only.

Here is the [sphere test file](test_scenes/primitives/sphere/sphere.json) as an example:

```json
{
  "sceneParameters": {
    "geometrySubtype": "sphere",
    "seed": 54321,
    "anari_objects": {
      "material": [
        {
          "subtype": "transparentMatte",
          "opacity": 0.5
        }
      ],
      "sampler": [
        {
          "subtype": "Image2D"
        }
      ]
    }
  },
  "permutations": {
    "primitiveCount": [1, 16],
    "/anari_objects/material/0/color": [null, [1.0, 1.0, 1.0], "ref_sampler_0"]
  },
  "variants": {
    "primitiveMode": ["soup", "indexed"]
  },
  "requiredFeatures": ["ANARI_KHR_GEOMETRY_SPHERE"],
  "metaData": {
    "1": {
      "bounds": {
        "world": [
          [0.8382989764213562, 0.43637922406196594, 0.5504830479621887],
          [0.9849825501441956, 0.5830627679824829, 0.6971666216850281]
        ]
      }
    },
    "16": {
      "bounds": {
        "world": [
          [-0.08789964765310287, -0.28438711166381836, -0.20388446748256683],
          [1.1669179201126099, 1.1838836669921875, 1.3429962396621704]
        ]
      }
    }
  }
}
```

- `sceneParameters`: This json objects contains all key value pairs which are passed to the C++ scene generator. The following parameters are supported. They are also documented in the [parameters() function in SceneGenerator.cpp](src/SceneGenerator.cpp/#L38):
  - `geometrySubtype`: Which type of geometry to generate. Possible values: `triangle`, `quad`, `sphere`, `curve`, `cone`, `cylinder`. Default: `triangle`
  - `shape`: Which shape should be generated. Currently only relevant for triangles and quads. Possible values: `triangle`, `quad`, `cube`. Default: `triangle`
  - `primitiveMode`: How the vertex data is arranged (`soup` or `indexed`). Default: `soup`
  - `primitiveCount`: Number of primitives to generate. Default: `1`
  - `vertexCaps`: Whether cones and cylinders should have caps (per vertex setting). Default: `false`
  - `globalCaps`: Whether cones and cylinders should have caps (global setting). Possible values: `none`, `first`, `second`, `both`. Default: `none`
  - `globalRadius`: Set the global radius property instead of using a per vertex one
  - `unusedVertices`: The last primitive's indices in the index buffer will be removed to test handling of unused/skipped vertices in the vertex buffer
  - `color`: Fill an attribute with colors. Possible values: `vertex.color`, `vertex.attribute0`, `primitive.attribute3` and similar
  - `opacity`: Fill an attribute with opacity values. Possible values: `vertex.attribute0`, `primitive.attribute3` and similar
  - `frame_color_type`: Type of the color channel. If empty, the color channel will not be used. Possible values: `UFIXED8_RGBA_SRGB`, `FLOAT32_VEC4`, `UFIXED8_VEC4`
  - `frame_albedo_type`: Render the albedo channel instead of the color channel if `KHR_FRAME_CHANNEL_ALBEDO` is supported. Possible values: `UFIXED8_VEC3`, `UFIXED8_RGB_SRGB`, `FLOAT32_VEC3`
  - `frame_normal_type`: Render the normal channel instead of the color channel if `KHR_FRAME_CHANNEL_NORMAL` is supported. Possible values: `FIXED16_VEC3`, `FLOAT32_VEC3`
  - `frame_primitiveId_type`: Render the primitive ID channel as colors instead of the color channel if `KHR_FRAME_CHANNEL_PRIMITIVE_ID` is supported. Possible values: `UINT32`
  - `frame_objectId_type`: Render the object ID channel as colors instead of the color channel if `KHR_FRAME_CHANNEL_OBJECT_ID` is supported. Possible values: `UINT32`
  - `frame_instanceId_type`: Render the instance ID channel as colors instead of the color channel if `KHR_FRAME_CHANNEL_INSTANCE_ID` is supported. Possible values: `UINT32`
  - `frame_depth_type`: Type of the depth channel. If empty, the depth channel will not be used. Possible values: `FLOAT32`
  - `image_height`: Height of the image. Default: `1024`
  - `image_width`: Width of the image. Default: `1024`
  - `attribute_min`: Minimum random value for attributes. Default: `0.0`
  - `attribute_max`: Maximum random value for attributes. Default: `1.0`
  - `primitive_attributes`: Whether primitive attributes should be filled randomly. Default: `false`
  - `vertex_attribtues`: Whether vertex attributes should be filled randomly. Default: `false`
  - `seed`: Seed for random number generator to ensure that tests are consistent across platforms. Default `0`
  - `spatial_field_dimensions`: A 3-integer array, describing the dimensions of all spatial fields in this test. If no spatial field is defined in `anariObjects`, a default one will be used.
  - `frameCompletionCallback`: When set to `true`, checks if `ANARI_KHR_FRAME_COMPLETION_CALLBACK` works correctly. Renders a green image on success and a red image otherwise.
  - `progressiveRendering`: When set to `true`, checks if `ANARI_KHR_PROGRESSIVE_RENDERING` works correctly. Renders a green image if more then 10 pixels differ from the previous rendering and a red image otherwise.
  - `camera_generate_transform`: Enables automatic adaptation of the camera position to the scene.
  - `anari_objects`: This JSON object can be used to set generic parameters for all ANARI objects except World. It holds arrays that each have their type name as their key. Each array should only have one element. This array contains objects with parameters for each ANARI object. The parameter `subtype` is mandatory for all object types that define it. `null` can be used to reset/use the default of a parameter (useful for permutations). To set a parameter to a reference to another object, use the `ref_` prefix, then add the type and index, e.g. `ref_sampler_0`. All other `sceneParameters` take precedence over the ones set using the generic `anari_objects`. This also implies that for geometries, subtypes can not be mixed and `subtype` in `anariObjects` need to match `geometrySubtype`. Additionally, if `instances` are used no objects are added to the world despite the instances. There is no need to create e.g. a default `surface` to display materials or geometry. If an object is missing, a default object is used instead. To set `Array1D` or `Array2D` parameters, the value needs to be a JSON object containing the type as key together with its value, e.g. `{"Array1D": [0, 1, 2, 3]}`.
-  `simplified_permutations`: When set to `true`, only the first value of each permutation list is used with permutations of other parameters, therefore reducing the amount of tests to N_0 + N_1 + ... instead of a cartesian product
- `permutations` and `variants`: These specify scene parameters where different values should be tested. Therefore, these JSON objects contain the name of a scene parameter and a list of values for each of them. The permutations specify changes which result in a different outcome (e.g. 1 or 16 primitives). The variants should not change the outcome, therefore only one reference rendering is needed for these (e.g. the soup and indexed variants should look the same). The cartesian product is performed for all parameters. In the current example this results in four test images: 1 primitive + soup, 1 primitive + indexed, 16 primitives + soup, 16 primitives + indexed; As well as two reference images: 1 primitive (soup or indexed), 16 primitives (soup or indexed). To permutate a parameter in an ANARI object, the object needs to be defined in `anariObjects` without the parameter. In the permutation or variant object a JSON pointer e.g. `"/anari_objects/material/0/color"` should be used as key as seen in the example.
- `requiredFeatures`: A list of features which need to be supported by the device to perform the test. Some features (e.g. perspective camera) which are needed for every test are implicitly required. This list should only contain the features which are explicitly tested. If the device does not support the feature, the test is skipped.
- `bounds_tolerance`: Specifies the percentage the bounds can vary in each dimension and still be considered correct.
- `thresholds`: Specifies the threshold for each comparison method and for this specific test only (can not be overwritten). The value consists of a JSON object with key value pairs consisting of the name of the metric and its threshold.
- `metaData`: This JSON object is automatically created while generating the reference images. It contains ANARI object properties for each permutation (currently only bounds information).

## Extending the Scene Generator

The C++ code is divided into the following source files:

- `main.cpp` contains the pybind11 definitions. For more information check the pybind11 documentation.
- `anariInfo.cpp` contains the refactored version of the `anariInfo.cpp` from the examples.
- `ctsQueries.cpp` is autogenerated by the generate_cts target.
- `anariWrapper.cpp` contains the functions which do not need to be applied to all scenes (query_features), the ANARI status function, and a wrapper around the SceneGenerator. This wrapper is needed to ensure the python callback is not garbage collected by pybind11.
- `SceneGenerator.cpp` inherits the `TestScene` class from `anari_test_scenes` and translates the test cases into ANARI objects.
- `PrimitiveGenerator.cpp` is used by the `SceneGenerator` to create the data needed for the ANARI geometries (e.g. vertex data, radii).

To add a new scene parameter, it just needs to be added to the JSON file of a test. To access it in C++ call

```cpp
auto newParameter = getParam<Type>("newParameterName", valueIfNotExists);
```

The `parameters()` function in `SceneGenerator.cpp` documents all available parameters. It should be extended while adding new parameters. The scene generator has two important functions: `commit()` sets up the scene by creating the ANARI objects after all parameters are set. `renderScene()` is only called by the `render_scenes()` python function and creates the default camera based on the image resolution, the renderer based on the specified subtype, the frame with the provided data types for each channel and finally renders the scene and returns the pixel data for each channel. The logic for new parameters should generally be added in one of the two functions.

`resetSceneObjects()` resets all ANARI objects but does not reset the parameters. It is used after a permutation/variant.
`resetAllParameters()` additionally resets all parameters and is used before a new scene is initialized.

To keep track of ANARI objects for e.g. property checks such as bounds, `m_anariObjects` can be used. It is a map which holds a vector of ANARI objects for each ANARI_TYPE. Do not release objects you added to this map, it will be done by the reset functions or the destructor.

If new functions need to be added to the scene generator and accessible from python, keep in mind that they also need to be added in `anariWrapper` and `main.cpp`.

## Creating the reference renderings/metadata

The `generateCTS` target can be build to generate all reference images, add the meta data to the test JSON files and generate the C++ ANARI query code. Currently the `helide` library is used to generate the references. To change this, modify `REFERENCE_LIB` in [CMakeLists.txt](CMakeLists.txt/#L10). One can also manually call the [`createReferenceData.py`](createReferenceData.py) script.\
This will clear and set the metaData objects of all tests in [`test_scenes`](test_scenes/) and place the reference renderings next to the corresponding json files with a `ref_` prefix.

## Extending the python scripts

The python code is divided into 4 files:

- [cts.py](cts.py): The main python file which parses all arguments and contains functions for each feature.
- [ctsUtility.py](ctsUtility.py): This file contains utility functions e.g. image comparison functions.
- [ctsReport.py](ctsReport.py): This file contains the functions to create the PDF report from JSON style input data.
- [createReferenceData.py](createReferenceData.py): This script can be used to create the reference data.

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

Another way to add a new metric temporarily is by using the `custom_compare_function` parameter of `compare_images` or `create_report`. This parameter can be set to a function with the following signature:

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

def apply_to_scenes(func, anari_library, anari_device = None, anari_renderer = "default", test_scenes = "test_scenes", only_permutations = False, check_features = True, use_generator = True,  *args)
```

It takes a function e.g. `function_per_scene` and calls it for each scene or each scene permutation, if present. The function receives the parsed json test file, the C++ sceneGenerator object, the name of the ANARI renderer, the file location of the test, the name of the test, the current permutation and variant or empty strings if none exist.
The return value is stored in a dictionary as a value with the key being the test's name + the permutation/variant string.

The `only_permutations` parameter of `apply_to_scenes` can be used to ignore variants. This is relevant for the reference creation. `check_features` can be set to `False` to run all tests even if the required features are not implemented. `use_generator` specifies if the C++ SceneGenerator should be initialized or not. In the latter case, `sceneGenerator` is set to `None`. Additional parameters can be added (denoted as `*args`) and will be passed directly to the apply function.

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
      "env": { "PYTHONPATH": "${workspaceRoot}" },
      "args": ["render_scenes", "helide"]
    },
    {
      "name": "Python: CTS REF",
      "type": "python",
      "request": "launch",
      "program": "${workspaceFolder}/createReferenceData.py",
      "cwd": "${workspaceFolder}",
      "console": "integratedTerminal",
      "env": { "PYTHONPATH": "${workspaceRoot}" },
      "args": []
    }
  ]
}
```

Open the `cts` folder in VS Code. Add `/.vscode/launch.json` and run `Python C++ Debugger: CTS` to start debugging. Use `args` to invoke different python functions.\
 `Python C++ Debugger: CTS REF` can be used to debug `createReferenceData.py`.
