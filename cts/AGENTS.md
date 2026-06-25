# AGENTS.md

This file provides guidance to coding agents working in the CTS. See the parent
`AGENTS.md` for overall SDK context, and `CONTEXT.md` here for the vocabulary
(Test / Axis / Permutation / Variant / Case / Channel / Ground truth / Workdir).

## Overview

The CTS (Conformance Test Suite) validates ANARI device implementations against
a reference device (helide). It is a self-contained C++ command-line tool,
`anariCts`, that renders each test *and* scores it against generated
ground truth, plus a thin Python reporting layer (`ctsReport.py`) that only
reads the results the tool writes. The two communicate through files only — there
is no pybind11, no JSON scene format, and no Python orchestration (these were
removed; see `docs/adr/0001`–`0002`).

## Build Commands

```bash
# From the SDK root build directory. The tool builds under BUILD_CTS or BUILD_TESTING.
cmake -DBUILD_CTS=ON -DBUILD_HELIDE_DEVICE=ON ..
cmake --build . -j

# glTF asset tests are gated separately (decoupled from BUILD_CTS):
cmake -DBUILD_CTS=ON -DBUILD_HELIDE_DEVICE=ON -DCTS_ENABLE_GLTF=ON ..
# ...and need the optional encodings for full texture coverage:
#   -DUSE_DRACO=ON -DUSE_KTX=ON -DUSE_WEBP=ON
```

Relevant targets: `anariCts` (the CTS tool), `anari_test_scenes` (the library
holding the catalog + runner), `anariCatalogTests` (the unit tests).

### Running locally

A stale install of the anari libs on `LD_LIBRARY_PATH` can shadow a fresh build
("undefined symbol" at runtime). Prepend the build dir when running:

```bash
cd build
LD_LIBRARY_PATH="$PWD:$LD_LIBRARY_PATH" ./anariCts run helide --workdir /tmp/run
LD_LIBRARY_PATH="$PWD:$LD_LIBRARY_PATH" ./anariCatalogTests
```

## Running the suite

```bash
anariCts generate --workdir myrun      # ground truth from the reference device (helide)
anariCts run helide --workdir myrun    # render + score a candidate against it
python ../cts/ctsReport.py report myrun [--pdf out.pdf]
python ../cts/ctsReport.py diff runA runB [--json]

anariCts list [--filter <pat>]         # enumerate the catalog
anariCts query-features <device>       # device extensions
anariCts query-metadata [--filter <pat>]   # catalog metadata as JSON (no device)
anariCts query-device-info <device>    # device introspection: object subtypes + parameter metadata
anariCts check-properties <device>     # per test: runnable vs skipped + missing features
```

`query-device-info` is genuine device introspection (the old `anariInfo`
capability): `--type`/`--subtype` restrict by name substring, `--skip-parameters`
lists subtypes only, `--info` adds per-parameter default/min/max/values/
description. It is distinct from `query-metadata`, which dumps the catalog's shape.

`--filter` is a substring or glob (`*`,`?`) matched case-insensitively over a
test's `<category>/<test>` id. `run` and `report` exit non-zero on any failure.

## Architecture

```
anariCts                 CLI dispatch: cts/tool/main.cpp
  Catalog                 in-C++ registry of every Test (one per-category file)
  Runner                  build world -> render each Channel -> generate GT or score vs GT -> sidecar
  Metrics                 SSIM / PSNR (single source of metrics, ADR-0004)
  Workdir / Sidecar       results/ ground_truth/ assets/ tree; per-Case sidecar JSON (ADR-0003)
ctsReport.py              reads the sidecar tree only; report + device diff
```

### Key source files (`src/anari_test_scenes/cts/`)

**Harness / runner:**
- `TestDef.h` — a registered Test: `build` (world), optional `cameraBuild` /
  `rendererBuild` build hooks (ADR-0006) and `behaviorCheck` hook, axes,
  required features, thresholds (test-wide + per-channel), channels.
- `TestBuilder.{h,cpp}` — the `makeTest(...)` fluent builder. Note the verb is
  `requireFeatures()`, not the C++20 keyword `requires`.
- `Axis.h` / `Case.{h,cpp}` / `Expansion.{h,cpp}` — axes expand into Cases
  (cartesian, or one-factor-at-a-time when `simplified`); variants share a
  ground-truth key, permutations get distinct ones.
- `Catalog.{h,cpp}` / `Filter.{h,cpp}` — the registry and its substring/glob filter.
- `BuildContext.{h,cpp}` / `Value.{h,cpp}` — per-Case axis values handed to
  `build()`, backed by `helium::ParameterizedObject`.
- `Runner.{h,cpp}` — builds, renders, and scores Cases; owns the camera/renderer
  defaults and the per-Channel readback; runs the behavior hook for behavioral tests.
- `FrameFormats.{h,cpp}` — resolves a Case's per-Channel output `ANARIDataType`
  from its `frame_<channel>_type` axis (color/albedo/normal); pure + unit-tested.
- `Metrics.{h,cpp}` / `Image.{h,cpp}` — SSIM/PSNR and PNG I/O (stb).
- `Workdir.{h,cpp}` / `Sidecar.{h,cpp}` — the results tree layout and the
  versioned per-Case sidecar contract (carries the producing `device` identity
  since schema v2; `Workdir` also keys the per-channel diff/threshold images).
- `DeviceInfo.{h,cpp}` — device introspection (`query-device-info`): subtypes +
  parameter metadata as text, returned (not printed) so it is unit-testable.
- `WorldBuilder.{h,cpp}` + `generators/` — shared world-build helpers lifted
  from the old `SceneGenerator::commit()`.

**Tests (the catalog):** `cts/tests/<category>.cpp` (geometry, material, sampler,
light, camera, frame, renderer, instance, volume) each define a
`register<Category>Tests(Catalog&)`; `cts/tests/gltf.cpp` is the glTF
asset-scanning factory (gated on `ENABLE_GLTF`). `BuiltinTests.cpp` wires them
all into the catalog.

**Unit tests:** `tests/unit/test_cts_*.cpp` (catalog/expansion/filter/builder,
metrics, results/sidecar/workdir, runner). Pure tests are tagged `[cts]`;
device-backed ones add `[helide]` and self-skip if no device loads.

**Python:** `ctsReport.py` — `report` and `diff` over the sidecar tree.

## Extending the CTS

**New Test:** add a `makeTest(...)....registerInto(catalog)` to the relevant
`cts/tests/<category>.cpp`. Author the world in `build()` with ANARI C++ calls
plus the `WorldBuilder.h` helpers; declare axes, required features, channels.

**New per-Case axis the runner must act on** (e.g. a new output format): resolve
it in `FrameFormats.cpp` (pure, unit-test it) and honor it in `Runner.cpp`.

**New metric:** add it to `Metrics.{h,cpp}`, record it in the runner's
`ChannelResult`, and add it to the `METRICS` list in `ctsReport.py` so reports
and diffs surface it. There is exactly one metric implementation, in C++.

**Sidecar schema change:** the sidecar is a versioned cross-language contract
(ADR-0003). A purely incidental optional field (read with a default on the
Python side) need not bump `kSidecarSchemaVersion`. Bump it when a change is
either breaking *or* semantically significant to consumers — i.e. a field they
should know to rely on — and update the Python reader in lockstep. v2 did the
latter: it added the `device` object that the device-diff now keys runs on
(plus the optional per-channel `diffImage`/`thresholdImage` debug paths), so the
reader targets v2 and warns on older sidecars, prompting a regenerate.
