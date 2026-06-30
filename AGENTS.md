# AGENTS.md

This file provides guidance to coding agents working in this repository.

## Overview

ANARI-SDK is the Khronos Group's implementation of the ANARI rendering API — a vendor-neutral, scene-graph-based API for 3D rendering. The SDK provides the core frontend library, reference device implementations, test infrastructure, and utilities for downstream device implementors.

## Build Commands

```bash
# Configure (typical development build)
mkdir build && cd build
cmake -DBUILD_EXAMPLES=ON -DBUILD_TESTING=ON -DBUILD_HELIDE_DEVICE=ON ..

# Build
cmake --build . --config Release

# Install to a prefix
cmake --install . --prefix /path/to/install

# Run all tests (render tests require ANARI_LIBRARY=helide)
ANARI_LIBRARY=helide ctest

# Run specific test categories
ctest -R unit_test
ANARI_LIBRARY=helide ctest -R render_test
ctest -R anariTutorial

# Run a single unit test binary directly (after building)
./tests/anariUnitTests
```

Key CMake options:
- `BUILD_SHARED_LIBS` (ON) — shared vs. static libraries
- `BUILD_VIEWER` (OFF) — requires SDL3 or GLFW
- `BUILD_CTS` (OFF) — conformance test suite, requires Python 3.9+
- `BUILD_HELIDE_GPU_DEVICE` (ON) — GPU-accelerated example device, requires SDL3 3.2+ and glslangValidator
- `BUILD_REMOTE_DEVICE` (OFF) — experimental MPI/network device
- `BUILD_HDANARI` (OFF) — experimental OpenUSD Hydra delegate
- `BUILD_CAT` (OFF) — capability analysis tool

## Code Style

C99 and C++17 are required. A `.clang-format` file enforces Google style with an 80-column limit and 2-space indentation. Run `clang-format -i <file>` before committing changes to C/C++ source.

## Architecture

### Layered Design

```
Application Code
    ↓  (anari.h C API)
Frontend Library  (src/anari/)
    ↓  (DeviceImpl / LibraryImpl interfaces)
Helium Base Layer (src/helium/)   ← optional but recommended
    ↓
Concrete Device  (e.g., src/devices/helide/)
    ↓
Rendering Backend (e.g., Embree for helide)
```

### Key Components

**`src/anari/`** — The core frontend library. Exposes the public `anari.h` C API, handles device loading via `anariLoadLibrary()`, and provides C++ convenience wrappers. Some source files here are generated from JSON specs by `code_gen/`.

**`src/helium/`** — Optional base abstractions for device implementors. Provides `BaseDevice`, `BaseObject`, `BaseArray`, `BaseFrame`, and utilities for parameter management, deferred commits, timestamp tracking, and property queries. Most device implementations should build on Helium rather than implementing raw interfaces.

**`src/devices/helide/`** — The reference ray-tracing device built on Intel Embree. Implements the full ANARI object hierarchy (geometries, materials, lights, cameras, renderers, volumes, spatial fields, samplers). Study this as the canonical example of a Helium-based device.

**`src/devices/helide_gpu/`** — An in-progress GPU-accelerated example device built on SDL3's cross-platform GPU abstraction. Uses a dedicated GPU thread for all SDL3_gpu commands, compiles GLSL shaders to SPIR-V at build time, and supports SSAO post-processing. Requires SDL3 3.2+, `glslangValidator`, and optionally `spirv-cross` for Metal/MSL support.

**`src/devices/debug/`** — A passthrough wrapper device that validates API usage without modifying rendering behavior.

**`src/devices/sink/`** — A null device that accepts all API calls and discards them. Useful for benchmarking API overhead in isolation.

**`src/anari_test_scenes/`** — Procedurally generated test scenes (categories: demo, test, perf, file-based). Used by the viewer, CAT, and CTS.

**`src/anari_viewer/`** — ImGUI-based interactive viewer library. Shared by viewer examples; supports SDL3 and GLFW backends.

**`code_gen/`** — Python scripts that generate device query implementations and wrapper code from JSON specification files. Run these when the ANARI spec changes. Key scripts:
- `generate_queries.py <device_definitions.json>` — generates object/parameter query implementations for a device
- `generate_headers.py` — regenerates public C/C++ headers from the spec
- Downstream devices invoke `anari_generate_queries()` in CMake (provided via the `code_gen` find_package component) rather than calling scripts directly.

**`cts/`** — Conformance Test Suite. A self-contained C++ tool, `anariCts` (entry point `cts/src/main.cpp`, built with `BUILD_CTS=ON`), renders each test and scores it against generated ground truth; a thin Python reporting layer (`ctsReport.py`) reads the results. The harness/runner/Test definitions under `src/anari_test_scenes/cts/` compile into the internal static `anari_cts_core` target; `anariCts` and `anariCatalogTests` link that core. (The former pybind11/Python orchestration was removed.)

### Object Commit Model

ANARI uses an explicit commit model: objects are modified via `anariSetParameter()` and changes take effect only after `anariCommitParameters()`. In Helium, `BaseObject::commitParameters()` is the override point. The `ParameterizedObject` base tracks which parameters have been set and provides typed `getParam<T>()` accessors.

### Device Loading

Devices are loaded as shared libraries at runtime via `anariLoadLibrary("helide", ...)`. The library must export `anari_library_helide_entrypoint` (using the `ANARI_DEFINE_LIBRARY_ENTRYPOINT` macro). The `ANARI_LIBRARY` environment variable can override which library is loaded.

## Testing

Unit tests use **Catch2** and live in `tests/unit/`. They test Helium internals (`AnariAny`, `ParameterizedObject`, `RefCounted`).

Render tests in `tests/render/` compare rendered output against reference images for scenes like `cornell_box`, `gravity_spheres_volume`, `instanced_cubes`, etc. Set `ANARI_LIBRARY=helide` when running render tests.

## Utilities

**`anariInfo`** (built with `BUILD_EXAMPLES=ON`) — introspects a loaded device and prints its supported object types, parameters, and extensions. Useful for verifying device capabilities:
```bash
ANARI_LIBRARY=helide anariInfo
```

## Implementing a New Device

1. Use `examples/empty_helium_device/` as a starting template.
2. Subclass `helium::BaseDevice` and implement `commitParameters()` plus object factory methods.
3. Use `code_gen/` scripts to generate query implementation stubs from the spec.
4. Reference `src/devices/helide/` for a complete, working example.

## CI

GitHub Actions (`.github/workflows/anari_sdk_ci.yml`) runs on push/PR to `next_release` and `main`. Matrix: Ubuntu 22.04/24.04 and Windows (Release + Debug), macOS (Release only). Tests require `ANARI_LIBRARY=helide`.
