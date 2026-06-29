# AGENTS.md

This file provides guidance to coding agents working in this repository.

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
- `CTS_ENABLE_GLTF` — also include the loader when a CTS consumer is built
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

### Conformance Test Suite harness (`cts/`)

The revamped CTS lives in the **`anari::cts`** sub-namespace (distinct from the
`anari::scenes` public API and the `registerScene` scene registry). It has no
ANARI device dependency except the world-build helpers. See `cts/CONTEXT.md` for
the glossary and `cts/docs/adr/` for the design decisions. These sources compile
only into the internal static `anari_cts_core` target when `BUILD_CTS` or
`BUILD_TESTING` creates a CTS consumer; they are not part of the installed
`anari_test_scenes` library or its public interface.

| Type | Purpose |
|---|---|
| `Axis` / `AxisKind` | A parameter dimension; `Permutation` (distinct ground truth) or `Variant` (shared ground truth) |
| `TestDef` + `makeTest()` `TestBuilder` | A catalog Test: build fn, axes, required features, thresholds, channels. Fluent builder; use `requireFeatures()` (not `requires`, a C++20 keyword) |
| `Case` + `expand()` | One resolved combination; full cartesian or simplified one-factor-at-a-time. `id()` (all values) vs `groundTruthKey()` (permutation values only) |
| `BuildContext` | Carries a Case's axis values to `build()`, read typed-by-name over `helium::ParameterizedObject` |
| `Catalog` + `Filter` | The in-C++ Test registry: register/list/filter/categories + feature gating |
| `GeometryLayout.h` / `GeometryBuilder.h` | Typed geometry specifications, pure deterministic Layout generation, and focused ANARI geometry publication |
| `ParameterBinding.h` | Explicit constant, attribute, sampler, and unset material bindings with checked publication |
| `*Builder.h` | Focused helpers for surfaces, lights, samplers, volumes, instances, views, and World assembly |

Unit tests: `tests/unit/test_cts_catalog.cpp` (pure, device-free) and
`tests/unit/test_cts_worldbuilder.cpp` (helide-backed smoke tests, skipped if no
device loads), both in the `anariCatalogTests` target.
