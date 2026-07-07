# AGENTS.md

This file provides guidance to coding agents working in the CTS. See the parent
`AGENTS.md` for overall SDK context, and `CONTEXT.md` here for the vocabulary
(Test / Axis / Permutation / Variant / Case / Channel / Ground truth / Workdir).

## Overview

The CTS (Conformance Test Suite) validates ANARI device implementations against
a reference device (helide). It is a self-contained C++ command-line tool,
`anariCts`, that renders each test, scores it against generated ground truth,
*and* reports on the results tree (`report`, ADR-0008). A thin Python
layer (`ctsReport.py`) is retained only for the one output not worth porting to
C++ — the `reportlab` PDF. Everything communicates through files only — there is
no pybind11, no JSON scene format, and no Python orchestration (these were
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

Relevant targets: `anariCts` (the CTS tool), `anari_cts_core` (the internal
static library holding the catalog + runner), `anari_test_scenes` (shared scene
and generator utilities), and `anariCatalogTests` (the unit tests).

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
anariCts run rtx --workdir myrun -p ambientSample=4   # custom renderer params
anariCts report myrun [--all] [--html out.html] [--embed]  # text + optional HTML
python ../cts/ctsReport.py report myrun --pdf out.pdf      # PDF only

anariCts list [--filter <pat>]         # enumerate the catalog
anariCts query-features <device>       # device extensions
anariCts query-metadata [--filter <pat>]   # catalog metadata as JSON (no device)
anariCts query-device-info <device>    # device introspection: object subtypes + parameter metadata
anariCts check-properties <device>     # per test: runnable vs skipped + missing features
```

`query-device-info` is genuine device introspection (the old `anariInfo`
capability): `--type`/`--subtype` restrict by name substring, `--skip-parameters`
lists subtypes only, `--info` adds per-parameter default/min/max/use/values/
element types/description/source extension. It is distinct from
`query-metadata`, which dumps the catalog's shape.

`--filter` is a substring or glob (`*`,`?`) matched case-insensitively over a
test's `<category>/<test>` id. `run` and `report` exit non-zero on any failure.

## Architecture

```
anariCts                 CLI dispatch: cts/src/main.cpp
  anari_cts_core          internal static CTS implementation
    DeviceIntrospection   frontend utility for query-device-info
    anari_test_scenes     shared scene and generator utilities
  Catalog                 in-C++ registry of every Test (one per-category file)
  Runner                  build world -> render each Channel -> generate GT or score vs GT -> sidecar
  Metrics                 SSIM / PSNR (single source of metrics, ADR-0004)
  Workdir / Sidecar       results/ ground_truth/ assets/ tree; per-Case sidecar JSON (ADR-0003)
  Report / HtmlReport     read the results tree -> text / HTML summary (ADR-0008)
ctsReport.py              reads the results tree too; retained only for the PDF
```

### Key source files (`src/anari_test_scenes/cts/`)

**Harness / runner:**
- `TestDef.h` — a registered Test: human-readable `description`, `build`
  (world), optional `cameraBuild` / `rendererConfig` hooks (ADR-0006) and
  `behaviorCheck` hook, axes, required features, thresholds (test-wide +
  per-channel), channels.
- `TestBuilder.{h,cpp}` — the `makeTest(...)` fluent builder. Note the verb is
  `requireFeatures()`, not the C++20 keyword `requires`.
- `Axis.h` / `Case.{h,cpp}` / `Expansion.{h,cpp}` — axes expand into Cases
  (cartesian, or one-factor-at-a-time when `simplified`); variants share a
  ground-truth key, permutations get distinct ones.
- `Catalog.{h,cpp}` / `Filter.{h,cpp}` — the registry and its substring/glob filter.
- `BuildContext.{h,cpp}` / `Value.{h,cpp}` — per-Case axis values handed to
  `build()`, backed by `helium::ParameterizedObject`.
- `Runner.{h,cpp}` — builds, renders, and scores Cases; owns renderer creation
  and baseline configuration, owns the default camera, and runs the behavior
  hook for behavioral tests.
- `FrameReadback.{h,cpp}` — validates mapped frame descriptors and converts
  supported per-Channel formats behind the shared map/unmap boundary.
- `FrameFormats.{h,cpp}` — resolves a Case's per-Channel output `ANARIDataType`
  from its `frame_<channel>_type` axis (color/albedo/normal); pure + unit-tested.
- `RendererParams.{h,cpp}` — pure parsing of CLI `--renderer-param NAME=VALUE`
  overrides into ANARI-typed bytes (unit-tested). The runner resolves each
  name's type from device introspection (falling back to inference) and sets it
  on every Case's renderer before the Test's own `rendererConfig`, so it stays
  device-agnostic and a renderer Test still wins on conflict.
- `Metrics.{h,cpp}` / `Image.{h,cpp}` — SSIM/PSNR and PNG I/O (stb).
- `Workdir.{h,cpp}` / `Sidecar.{h,cpp}` — the results tree layout and the
  versioned per-Case sidecar contract (carries the producing `device` identity
  since schema v2 plus optional Test descriptions; `Workdir` also keys the
  per-channel diff/threshold images). `Sidecar` both writes (`toJson`/
  `writeSidecar`) and reads (`fromJson`/`readSidecar`) the contract.
- `Report.{h,cpp}` — reads the results tree (`loadResults`), aggregates it
  (`summarize`, `runDevice`), and emits the text summary. `kReportMetrics` is
  the single list of reported metrics. Pure; unit-tested.
- `HtmlReport.{h,cpp}` — renders the self-contained interactive HTML report
  (inline CSS/JS, optional base64 `--embed`); pure `htmlEscape`/`base64Encode`
  are unit-tested.
- `GeometryLayout.{h,cpp}` — pure deterministic Layout generation using typed
  shape and primitive-mode enums.
- `GeometryBuilder.{h,cpp}` — geometry-family specifications and ANARI
  publication; one entry point per supported family.
- `ParameterBinding.{h,cpp}` — checked constant/attribute/sampler/unset
  material bindings. `SurfaceBuilder`, `LightBuilder`, `SamplerBuilder`,
  `VolumeBuilder`, `InstanceBuilder`, `ViewBuilder`, and `WorldBuilder` own the
  remaining focused construction responsibilities.

`query-device-info` is a thin CLI adapter over the shared frontend utility in
`src/anari/DeviceIntrospection.cpp`; the same utility backs `anariInfo`.

**Tests (the catalog):** `cts/tests/<category>.cpp` (geometry, material, sampler,
light, camera, frame, renderer, instance, volume) each define a
`register<Category>Tests(Catalog&)`; `cts/tests/gltf.cpp` is the glTF
asset-scanning factory (gated on `ENABLE_GLTF`). `BuiltinTests.cpp` wires them
all into the catalog.

**Unit tests:** `tests/unit/test_cts_*.cpp` (catalog/expansion/filter/builder,
metrics, results/sidecar/workdir, report/html, runner). Pure tests are
tagged `[cts]`; device-backed ones add `[helide]` and self-skip if no device
loads.

**Python:** `ctsReport.py` — the `report --pdf` path only; it reads the results
tree exactly as the C++ `report` does. Text and HTML live in C++.

## Extending the CTS

**New Test:** add a `makeTest(...)....registerInto(catalog)` to the relevant
`cts/tests/<category>.cpp`. Author the world in `build()` with ANARI C++ calls
plus the focused `*Builder.h` helpers; add a short `description()` of the
primary check, then declare axes, required features, and channels.

**New per-Case axis the runner must act on** (e.g. a new output format): resolve
it in `FrameFormats.cpp` (pure, unit-test it) and honor it in `Runner.cpp`.

**New metric:** add it to `Metrics.{h,cpp}`, record it in the runner's
`ChannelResult`, add it to `kReportMetrics` in `Report.h` (the C++ text/HTML),
and to the `METRICS` list in `ctsReport.py` (the PDF). There is exactly one
metric *implementation*, in C++; the two reported-metric lists are just display
order for two independent readers of the one contract.

**Sidecar schema change:** the sidecar is a versioned cross-language contract
(ADR-0003) with two readers — the C++ `Sidecar` reader (`fromJson`) behind
`report`, and `ctsReport.py` for the PDF. A purely incidental optional field
(read with a default on both sides) need not bump `kSidecarSchemaVersion`. Bump
it when a change is either breaking *or* semantically significant to consumers —
i.e. a field they should know to rely on — and update both readers in lockstep.
v2 did the latter: it added the `device` object that labels a run (plus the
optional per-channel `diffImage`/`thresholdImage` debug paths), so the readers
target v2 and warn on older sidecars, prompting a regenerate.
