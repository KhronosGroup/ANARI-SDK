# ANARI 1.0 CTS Design Document <!-- omit in toc -->

- [Introduction](#introduction)
- [Architecture](#architecture)
  - [C++ CTS library](#c-cts-library)
  - [Python CTS API](#python-cts-api)
- [Features](#features)
  - [Render a set of known test scenes](#render-a-set-of-known-test-scenes)
    - [Define test scene format](#define-test-scene-format)
    - [Python API](#python-api)
    - [C++](#c)
    - [Example output](#example-output)
  - [Image comparison “smoke tests”](#image-comparison-smoke-tests)
    - [Python API](#python-api-1)
    - [Comparison methods](#comparison-methods)
    - [Example output](#example-output-1)
  - [Verification of object/parameter info metadata](#verification-of-objectparameter-info-metadata)
    - [Python API](#python-api-2)
    - [C++](#c-1)
    - [Example output](#example-output-2)
  - [Verification of known object properties](#verification-of-known-object-properties)
    - [Python API](#python-api-3)
    - [C++](#c-2)
    - [Example output](#example-output-3)
  - [List core extensions implemented by a device](#list-core-extensions-implemented-by-a-device)
    - [Python API](#python-api-4)
    - [C++](#c-3)
    - [Example output](#example-output-4)
  - [Aggregated test results](#aggregated-test-results)
- [References](#references)

## Q&A <!-- omit in toc -->
- Starting the example viewer crashes on some Windows machines (read access violation on scene deletion)
- Depth test is defined as Euclidean distance (not normalized). If we want to use it as metric, a suitable range needs to be defined for all or each test scene.

## Introduction
- Goal and Motivation of this project
## Architecture
- Describes the architecture and dependencies of the individual components
### C++ CTS library
- Calls the ANARI API
- Sets up the test scenes
- Renders the conformance test images
- Queries extensions/metadata/properties

### Python CTS API
- Communicates with C++ CTS library
- Parses input parameters
- Compares test images to ground truth
- Creates PDF report or presents results to the user via CLI

## Features
### Render a set of known test scenes
#### Define test scene format
- Scenes should be easy to generate
- Scenes should cover edge cases
- Scenes should be usable for objective structural testing
#### Python API
- Lets user define parameters for the rendering
#### C++
- Setup ANARI and render scene to image
#### Example output
- Renderer images

### Image comparison “smoke tests”
#### Python API
- Lets user define parameters for the comparison tool
#### Comparison methods
Proposed methods:
- SSIM [^1]
- Depth test
<figure>
  <img src="./images/instanced_cubes_0.png" width="400"/>
  <figcaption>instanced_cubes_0 created with anariRenderTests</figcaption>
</figure>

#### Example output
- Conformance test report as PDF showing the differences between test images and ground truth

### Verification of object/parameter info metadata
#### Python API
- Executes the verification
- Presents the results to the user
#### C++
- Queries all items via Object introspection
#### Example output
- List of all available objects/parameter info metadata
### Verification of known object properties
#### Python API
- Executes the verification
- Presents the results to the user
#### C++
- Checks if queried property values match test scene input
#### Example output
- List of invalid property values

### List core extensions implemented by a device
#### Python API
- Executes the query
- Presents the results to the user
#### C++
- Checks availability of each core extension
#### Example output
- List of all available core extensions

### Aggregated test results
- Runs all tests and creates one report containing all test cases in a single PDF file

## References
[^1]:https://en.wikipedia.org/wiki/Structural_similarity