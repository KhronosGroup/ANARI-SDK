# AGENTS.md

This file provides guidance to coding agents working in this repository.

## Overview

`devices/helide_gpu` is an in-progress ANARI device implementation (`libanari_library_helide-gpu`) backed by **SDL3_gpu** (a cross-platform GPU abstraction introduced in SDL 3.2). It is a standalone CMake project that can also be built as part of the parent VisRTX tree.

## Build

### Standalone

```bash
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/path/to/install /path/to/devices/helide_gpu
cmake --build . --parallel
cmake --install .
```

### Dependencies

- CMake 3.11+, C++17
- `anari` (ANARI-SDK with `helium`)
- SDL3 3.2+ (`find_package(SDL3)`) — provides `SDL3::SDL3`
- `glslangValidator` in PATH — used at build time to compile GLSL shaders to SPIR-V

GLM is downloaded automatically via `anari_sdk_fetch_project()` at configure time — no system install required.

### Code generation

`HelideGPUDefinitions.json` drives `anari_generate_queries()` to produce `anari_library_helide-gpu_queries.h/.cpp`, which implement the device's ANARI query API (object subtypes, parameter info, extension list). Regenerated on every CMake configure + build.

### Shader compilation

CMake compiles `shaders/triangle.vert` and `shaders/triangle.frag` to SPIR-V via `glslangValidator`, then converts the SPIR-V binary to a C byte-array header using `cmake/bin2header.cmake`. The generated headers (`triangle_vert_spv.h`, `triangle_frag_spv.h`) land in the build directory and are included by `renderer/Renderer.cpp`.

## Directory structure

```
devices/helide_gpu/
├── array/          # Array1D, Array2D, Array3D, ObjectArray stubs
├── camera/         # Camera
├── cmake/          # bin2header.cmake helper
├── frame/          # Frame (per-frame render loop)
├── geometry/       # Geometry base
├── gpu/            # SDLGPUDevice wrapper (sdl3_gpu_device.h/.cpp)
├── light/          # Light base
├── material/       # Material base
├── renderer/       # Renderer (SDL3_gpu pipeline)
├── sampler/        # Sampler base
├── scene/          # Group, Instance, World
├── shaders/        # triangle.vert / triangle.frag (GLSL → SPIR-V at build time)
├── spatial_field/  # SpatialField base
├── surface/        # Surface
├── volume/         # Volume base
├── GLContextInterface.h/.cpp   # Status-reporting helper
├── Object.h/.cpp               # Base ANARI object + gpu_enqueue_method<>
├── TaskQueue.h                 # tasking::TaskQueue ring-buffer thread
├── HelideGPUDefinitions.json      # Drives ANARI query-API code generation
├── HelideGPUDevice.h/.cpp         # Top-level ANARI device
├── HelideGPUDeviceGlobalState.h/.cpp  # Per-device singleton (GPU thread + SDL device)
├── HelideGPULibrary.cpp           # ANARI library entry point
└── HelideGPUMath.h                # Math utilities (GLM wrappers)
```

## Architecture

### Threading model

All GPU commands **must** be submitted from the dedicated GPU thread. `HelideGPUDeviceGlobalState::gpu.thread` is a `tasking::TaskQueue` (a single fixed-size ring-buffer thread). Use `gpu_enqueue_method(object, &Class::gpu_method)` (free function in `Object.h`) or the member helper in `Frame` to post work to this thread. Methods named with the `gpu_` prefix run on the GPU thread; all others run on the calling thread.

### Key types

| Type | Role |
|------|------|
| `HelideGPUDevice` | Top-level ANARI device; owns `HelideGPUDeviceGlobalState`; lazily initializes on first API call via `initDevice()` |
| `HelideGPUDeviceGlobalState` | Singleton per device; holds the GPU thread, `SDLGPUDevice` wrapper, and extension list |
| `SDLGPUDevice` | Thin owner of `SDL_GPUDevice*`; `init()` calls `SDL_CreateGPUDevice`; `release()` destroys it |
| `Object` | Base for all ANARI objects; wraps `helium::BaseObject`; provides `deviceState()` accessor |
| `Frame` | Manages per-frame color texture + transfer buffer pair; drives the render loop via `gpu_renderFrame()` |
| `Renderer` | Holds SDL3_gpu graphics pipeline; provides `background()` color |

### Object lifecycle

ANARI's commit-based pattern is followed: `commitParameters()` reads parameters from the helium param store; `finalize()` enqueues GPU resource allocation. New ANARI object types (e.g., a triangle geometry) should:

1. Subclass the appropriate base (`Geometry`, `Material`, etc.)
2. Add a `XxxGPUState` struct for SDL3_gpu object handles
3. Override `commitParameters()` / `finalize()` with `gpu_` helpers dispatched via `gpu_enqueue_method`
4. Register the subtype string in `createInstance()` and in `HelideGPUDefinitions.json`

### GPU function calls

SDL3_gpu functions are accessed via the SDL3 API directly (`SDL_CreateGPUTexture`, etc.). The `SDL_GPUDevice*` is stored in `state.gpu.device` (a convenience alias for `state.gpu.sdlDevice.device`). Include `<SDL3/SDL_gpu.h>` (available transitively through `gpu/sdl3_gpu_device.h`) in files that need GPU types.

### SPIR-V shader binding convention (SDL3_gpu)

SDL3_gpu maps SPIR-V descriptor sets to resource slots as follows:

| Stage | Resource | set | binding |
|-------|----------|-----|---------|
| Vertex | Samplers+storage textures+storage buffers | 0 | 0..N |
| Vertex | Uniform buffers | 1 | 0..N |
| Fragment | Samplers+storage textures+storage buffers | 2 | 0..N |
| Fragment | Uniform buffers | 3 | 0..N |

Push data with `SDL_PushGPUVertexUniformData(cmd, slot, data, size)` — slot N maps to `set=1, binding=N` in the vertex shader SPIR-V.
Push data with `SDL_PushGPUFragmentUniformData(cmd, slot, data, size)` — slot N maps to `set=3, binding=N` in the fragment shader SPIR-V.

### MSL/Metal binding order

For MSL shaders SDL3_gpu requires a specific resource ordering within `[[buffer(N)]]`:
**uniform buffers first, then storage buffers** (see `SDL_CreateGPUShader` docs).

This means a vertex shader with 1 UBO and 8 SSBOs must map to:
- `[[buffer(0)]]` → uniform buffer (slot 0)
- `[[buffer(1..8)]]` → storage buffers (slots 0..7)

spirv-cross does **not** produce this ordering by default (it places set=0 SSBOs before set=1 UBOs). Shaders with both SSBOs and a UBO in the vertex stage require explicit `--msl-resource-binding` flags when invoking spirv-cross. See `compile_msl_header` call sites in `CMakeLists.txt` for the pattern.

## Code style

Google-style C++ (`clang-format`). Format with:
```bash
clang-format -i <file>
```
