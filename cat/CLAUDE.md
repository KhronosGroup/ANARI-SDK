# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

CAT (Capability Analysis Tool) is a performance benchmarking and capability analysis application for ANARI renderers. It supports both an interactive GUI mode (ImGUI + ImPlot) and a non-interactive CLI mode for automated performance data collection.

## Build Commands

```bash
# From ANARI-SDK root — CAT is off by default
cmake -DBUILD_CAT=ON -DBUILD_HELIDE_DEVICE=ON [other options] ..
cmake --build . --config Release

# Run in interactive GUI mode
ANARI_LIBRARY=helide ./anariCat run -g

# Run non-interactive benchmark (100 frames, max 60s, save CSV)
ANARI_LIBRARY=helide ./anariCat run -s perf:particles -n 100 -t 60 -m metrics.csv

# List available scenes / devices / renderers
ANARI_LIBRARY=helide ./anariCat list scenes
ANARI_LIBRARY=helide ./anariCat list devices
ANARI_LIBRARY=helide ./anariCat list renderers
```

Key CLI flags for `run` subcommand: `-g` (GUI), `-a` (animate), `-s category:scene`, `-l library`, `-r renderer`, `-n num-frames`, `-t timespan-secs`, `-c capture-every-N-frames`, `-m metrics-csv`, `--size W H`, `-o` (orthographic).

## Architecture

CAT inherits from `anari_viewer::Application` (provided by `src/anari_viewer/` in the SDK root), which supplies ImGUI window management and the render loop.

```
Application (Application.h/cpp)
├── CLI parsing via CLI11 (external/cli11/)
├── Device/renderer enumeration (ANARI queries)
├── Scene management via anari_test_scenes
├── Metrics collection (PerformanceMetrics.h, Timer.h/cpp)
└── UI windows
    ├── SceneSelector (windows/) — scene browser + parameter controls
    └── PerformanceMetricsViewer (windows/) — live ImPlot graphs
```

**`Application.cpp`** is the core file. It handles both the interactive GUI event loop (via `anari_viewer::Application`) and the non-interactive rendering loop. CLI11 argument parsing and all device/scene lifecycle management live here.

**`PerformanceMetrics.h`** defines the full metrics pipeline: `MetricType` enum, `ScrollingBuffer` for graph data, and `Recorder` for per-frame collection. Eight metrics are tracked per frame: `LATENCY_ANARI_DEVICE`, `DROPPED_FRAMES`, `LATENCY_PRESENTATION`, `LATENCY_APPLICATION`, `LATENCY_UI`, `TIME_SCENE_UPDATE`, `TIME_SCENE_BUILD`, and `TIMESTAMP`.

**`ui_layout.cpp`** configures the ImGUI dock layout. Edit this when adding or repositioning UI panels.

**External dependencies** are managed as CMake subdirectories under `external/`: CLI11 (header-only) and implot. These are not system packages — changes to them should be made carefully.

## Key Relationships

- Scenes come from `anari::anari_test_scenes`. Scene names use `category:name` format (e.g., `perf:particles`, `demo:cornell_box`).
- The viewer base class (`anari_viewer::Application`) owns the ANARI device, frame, and renderer. CAT configures them via the base class API.
- Non-interactive mode bypasses the ImGUI event loop entirely and drives the render loop manually in `Application::exec()`.
