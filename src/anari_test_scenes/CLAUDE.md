# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

`anari_test_scenes` is a library of procedurally generated test scenes used by the viewer, CTS, and render tests. Scenes are organized into four categories: `demo`, `test`, `perf`, and `file`.

## Build

This directory builds as part of the parent ANARI-SDK project. From the repo root:

```bash
mkdir build && cd build
cmake -DBUILD_TESTING=ON -DBUILD_HELIDE_DEVICE=ON ..
cmake --build .

# Run render tests (exercises test scenes)
ANARI_LIBRARY=helide ctest -R render_test
```

Optional CMake flags that affect this library:
- `VIEWER_ENABLE_GLTF` — include glTF file loader scene
- `USE_DRACO`, `USE_WEBP`, `USE_KTX` — glTF compression/texture extensions
- `USE_KOKKOS` — parallel processing backend

**C++ standard: C++20** (needed for designated initializers in aggregate structs).

## Architecture

### Public API (`anari_test_scenes.h`)

All external callers use the free functions in `anari::scenes::`:

```cpp
// Discovery
getAvailableSceneCategories()
getAvailableSceneNames(category)

// Lifecycle
SceneHandle createScene(device, category, name)
release(SceneHandle)

// Parameters (mirrors ANARI's set→commit model)
setParameter(SceneHandle, name, value)
commit(SceneHandle)

// Queries
getWorld(SceneHandle)   // anari::World ready to render
getBounds(SceneHandle)
getCameras(SceneHandle)
getParameters(SceneHandle)

// Animation
isAnimated(SceneHandle)
computeNextFrame(SceneHandle)
```

`SceneHandle` is a typedef for `TestScene *`, kept opaque to callers.

### Scene Registry (`anari_test_scenes.cpp`)

A lazy-initialized singleton `std::map<category, std::map<name, ConstructorFcn>>` holds all registered scenes. Registration happens via `registerScene()` calls in the initializer. `createScene()` looks up and invokes the constructor function.

### Base Class (`scenes/scene.h`)

`TestScene` inherits from `helium::ParameterizedObject`, which provides the typed `getParam<T>(name, default)` accessor and the set→commit parameter update flow.

Required overrides:
- `world()` — return the `anari::World` handle
- `commit()` — rebuild/update scene from current parameters

Optional overrides:
- `parameters()` — return `ParameterInfo` list for UI/tooling
- `cameras()` — return predefined camera positions
- `bounds()` — world-space bounding box
- `animated()` / `computeNextFrame()` — animation support

### Adding a New Scene

1. Create `scenes/<category>/my_scene.h` and `my_scene.cpp`.
2. Subclass `TestScene`, implement `world()` and `commit()`.
3. Define a factory function: `TestScene *sceneMyScene(anari::Device d) { return new MyScene(d); }`
4. In `anari_test_scenes.cpp`, add `registerScene("category", "my_scene", sceneMyScene);` to the initializer block.
5. Add the `.cpp` file to `CMakeLists.txt`.

Destructor must release all ANARI handles created by the scene (geometry, material, surface, world, etc.).

### Generators (`generators/`)

Reusable procedural utilities for scene construction:

| Class | Purpose |
|---|---|
| `PrimitiveGenerator` | Mesh generation: triangles, quads, spheres, cylinders, cones, curves; random attributes and transforms |
| `TextureGenerator` | Synthetic textures: ramps, checkerboards, gradients, normal maps, HDR |
| `ColorPalette` | Indexed color palette |
| `SceneGenerator` | Higher-level scene builder used by the CTS; supports arbitrary ANARI object creation and glTF loading |

### Scene Parameter Pattern

```cpp
// In commit():
const int count = getParam<int>("count", 1000);  // name, default
const float radius = getParam<float>("radius", 0.01f);

// Metadata for UI:
std::vector<ParameterInfo> parameters() override {
  return { makeParameterInfo("count", "Sphere count", 1000, 1, 1e6) };
}
```
