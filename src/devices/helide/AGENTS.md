# AGENTS.md

This file provides guidance to coding agents working in this repository.

## Overview

This is the **helide** reference ray-tracing device for the ANARI SDK, built on Intel Embree. It is the canonical example of a complete Helium-based ANARI device. See the root `AGENTS.md` for SDK-wide context and build commands.

## Object Hierarchy

All helide objects inherit from `helide::Object` → `helium::BaseObject`. Each concrete type has a static `createInstance(subtype, state*)` factory method called by `HelideDevice`.

```
HelideDevice
├── Camera: Perspective, Orthographic
├── Geometry: Triangle, Sphere, Quad, Curve, Cylinder, Cone
├── Material: Matte, PBM
├── Sampler: Image1D, Image2D, Image3D, PrimitiveSampler, TransformSampler
├── SpatialField: StructuredRegularField
├── Surface (binds Geometry + Material)
├── Volume → TransferFunction1D
├── Light (base only; shading is implemented inline in Renderer)
├── World → Instance → Group → {Surface[], Volume[]}
├── Frame
└── Renderer
```

## Embree Integration

Embree v4.3.3 is fetched via CMake from GitHub into `external/embree/`. The namespace is aliased to `embree_for_helide` (in the Embree CMakeLists) to avoid symbol conflicts. The static library is exposed as the `local_embree` CMake target.

**Scene hierarchy:**
- `World` owns the top-level Embree scene (`RTCScene` TLS)
- `Group` owns a bottom-level Embree scene (`RTCScene` BLS) per group of Surfaces
- Each `Geometry` subclass creates and owns an `RTCGeometry` handle
- `rtcCommitGeometry()` / `rtcCommitScene()` must be called after any geometry mutation

**Geometry types used:**
| Helide class | Embree type |
|---|---|
| Triangle | `RTC_GEOMETRY_TYPE_TRIANGLE` |
| Sphere | `RTC_GEOMETRY_TYPE_SPHERE_POINT` |
| Curve | `RTC_GEOMETRY_TYPE_ROUND_CUBIC_BSPLINE_CURVE` |
| Quad | `RTC_GEOMETRY_TYPE_QUAD` |
| Cylinder/Cone | custom triangle mesh |

## Rendering Pipeline

`Frame::renderFrame()` → async task → per-pixel `Renderer::renderSample(screen, ray, world)`:

1. `Camera::createRay(screen)` generates the primary ray
2. `rtcIntersect1(embreeScene, &rayHit, &args)` against the Embree TLS
3. On hit: look up `Surface` → `Material`; sample color/opacity via `Sampler`
4. Volume ray marching against `StructuredRegularField` if any volumes exist
5. Compose result into `PixelSample{color, depth, primID, objID, instID}`

`RenderingSemaphore` coordinates the async render with array mapping/unmapping on the host side.

## Renderer Debug Modes

The `mode` parameter on the `default` renderer subtype selects a visualization:

- `default` — ambient + eye light shading
- `primitiveId`, `objectId`, `instanceId` — ID buffer visualization
- `Ng`, `Ng.abs` — geometric normals
- `uvw` — ray/barycentric coordinates
- `backface`, `hitSurface`, `hitVolume`, `opacityHeatmap`, `testFrame`
- `geometry.color`, `geometry.attribute0`–`3` — raw attribute inspection
- Embree raw IDs: `embreePrimID`, `embreeGeomID`, `embreeInstID`

## Attribute System

Geometries support per-vertex/per-primitive attributes (`color`, `attribute0`–`3`). At a ray hit, `Geometry::getAttributeValue(attr, ray)` interpolates using barycentric coordinates. `Material::colorAttribute()` specifies which attribute feeds the surface color.

## Adding a New Geometry Type

1. Create `geometry/MyGeom.h/.cpp` inheriting from `Geometry`
2. Override `commitParameters()` to read ANARI parameters and `finalize()` to build the Embree geometry
3. Add `MyGeom` to `Geometry::createInstance()` dispatch in `Geometry.cpp`
4. Register the new subtype in `HelideDefinitions.json`
5. Re-run `anari_generate_queries()` (handled automatically by CMake)

## Device-Level Parameters

Set on `ANARIDevice` before creating objects:

| Parameter | Type | Default | Effect |
|---|---|---|---|
| `allowInvalidMaterials` | bool | true | Render surfaces with no valid material in magenta |
| `invalidMaterialColor` | float4 | [1,0,1,1] | Color used for invalid-material surfaces |

## Key Files

- `HelideDevice.h/cpp` — device class + all object factory methods
- `HelideGlobalState.h/cpp` — per-device state (Embree device handle, tick counter)
- `renderer/Renderer.h/cpp` — core shading/ray marching logic
- `frame/Frame.h/cpp` — async frame scheduling and output channel management
- `HelideDefinitions.json` — extension/parameter declarations (input to code-gen)
