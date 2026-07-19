# AGENTS.md

This file provides guidance to coding agents working in this repository.

## Overview

`anari_viewer` is a **source-only** ImGui-based viewer library for interactive ANARI rendering applications. It is intentionally unstable (no API stability guarantees) and is consumed by downstream projects via CMake as `anari::anari_viewer`. See `BUILD_VIEWER` in the root CMakeLists.txt.

Build it from the root:
```bash
cmake -DBUILD_VIEWER=ON -DBUILD_EXAMPLES=ON ..
cmake --build .
# Run the viewer example
ANARI_LIBRARY=helide ./examples/anariViewer
```

## Architecture

### Source-Only Library

All `.h` and `.cpp` files are installed together and consumed as an INTERFACE CMake target — there is no pre-compiled `libanari_viewer.so`. Downstream code includes the sources directly.

### Class Hierarchy

```
Application  (Application.h)
  └── [subclassed by downstream apps, e.g. examples/viewer/]
        ├── Viewport       (windows/Viewport.h)   ← ANARI rendering viewport
        ├── LightsEditor   (windows/LightsEditor.h)
        └── [custom Window subclasses]

Window  (windows/Window.h)
  ├── Viewport
  └── LightsEditor
```

### Application Lifecycle

`Application::run()` initializes SDL3 + ImGui and enters the main loop. Subclasses customize behavior via virtual hooks called each frame in order:

```
mainLoopStart → uiFrameStart → [buildUI on each Window] → uiFrameStart →
uiRenderStart → [SDL3 render] → uiRenderEnd → mainLoopEnd
```

Implement `setupWindows()` and `teardown()` at minimum. SDL window flags (e.g., high-DPI) are customizable via `sdlWindowFlags()`.

### Viewport / ANARI Integration

`Viewport` owns the ANARI frame, camera, and renderer handles. Key flow each frame:
1. `reshape()` — recreates SDL texture and ANARI frame on resize
2. `updateCamera()` — commits camera parameters when `Orbit` reports a change
3. `startNewFrame()` — calls `anari::render()` (non-blocking)
4. `updateImage()` — reads back frame pixels via `anariMapFrame` and uploads to SDL texture
5. `ImGui::Image()` — displays the texture (Y-axis flipped)

`Viewport::setWorld()` sets the ANARI world and resets the camera by querying `anariGetProperty(world, "bounds", ...)`.

Available renderers are discovered at construction via `anariGetObjectSubtypes(device, ANARI_RENDERER)`.

### Camera (Orbit.h)

Arcball/orbit manipulator. Uses a token-based change-tracking system (`UpdateToken`) so camera parameters are only committed to ANARI when the camera actually moves. Supports 6 axis orientations (±X/Y/Z up).

### Dynamic Parameter UI (ui_anari.h)

`ui::parseParameters()` queries a device for object parameter metadata (type, default, min, max). `ui::buildUI()` generates the appropriate ImGui widgets. This is how the renderer parameter editor and other parameter panels are built without hardcoding widget types.

### LightsEditor

Supports directional, point, spot, and HDRI lights. Designed for **multiple simultaneous devices** — each `Light` struct holds one `anari::Light` handle per device. HDRI file loading uses SDL3's file dialog API.

### External Dependencies

- **ImGui** (v1.91.7-docking) + SDL3 backend — fetched via CMake FetchContent if not found
- **SDL3** (v3.2.12) — fetched automatically if `find_package(SDL3)` fails
- **stb_image / stb_image_write** — bundled under `external/stb_image/`
- **tinyexr** — bundled for EXR support (excluded on Windows)

## Extending the Viewer

To create a custom viewer application:

1. Subclass `Application`, override `setupWindows()` to construct `Viewport`, `LightsEditor`, and any custom `Window` subclasses
2. Subclass `Window` and override `buildUI()` for custom ImGui panels
3. Use `Viewport::setWorld()` to swap scenes
4. Use `Viewport::addOverlay()` for custom screen-space overlays (implement `Viewport::Overlay`)
5. See `examples/viewer/` for a complete working example using `anari_test_scenes`
