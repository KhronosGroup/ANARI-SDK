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



## Introduction

## Architecture

### C++ CTS library
For calling the ANARI API, setting up the test scenes, rendering the images, query extensions/metadata/properties.

### Python CTS API
Communication with C++ CTS library, parsing of parameters, performing image diffs, creating report, presenting results to the user

## Features
### Render a set of known test scenes
#### Define test scene format
Scenes should be easy to generate
#### Python API
How to define parameters for the rendering
#### C++
Setup ANARI and render scene to image
#### Example output

### Image comparison “smoke tests”
#### Python API
How to call the comparison tool
#### Comparison methods
Which metrics are used for comparison and how to configure them (e.g. SSIM or depth test)
#### Example output

### Verification of object/parameter info metadata
#### Python API
How to execute the verification, presenting results to the user
#### C++
Query all items via Object introspection
#### Example output

### Verification of known object properties
#### Python API
How to execute the verification, presenting results to the user
#### C++
Check if queried property values match test scene **inputT
#### Example output

### List core extensions implemented by a device
#### Python API
How to execute the query, presenting results to the user
#### C++
Check for each core extension if it is enabled
#### Example output

### Aggregated test results
Run all tests and create one report containing all use cases in a single PDF file.

## References