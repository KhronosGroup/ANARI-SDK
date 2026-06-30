# CTS

The Conformance Test Suite (CTS) validates an ANARI device implementation by
rendering a known battery of scenes and comparing each render against
ground-truth images produced by a reference device (helide). It is a
self-contained C++ command-line tool, `anariCts`, plus a thin Python reporting
layer (`ctsReport.py`) that only reads the results the tool writes.

It can:

- enumerate the test catalog (`list`, `query-metadata`),
- generate ground-truth images with the reference device (`generate`),
- render and score a candidate device against that ground truth (`run`),
- report which extensions a device implements (`query-features`) and which
  tests it can run vs. will skip (`check-properties`),
- summarize a run, optionally as a PDF, and diff two devices (`ctsReport.py`).

See [CONTEXT.md](CONTEXT.md) for the vocabulary (Test, Axis, Permutation,
Variant, Case, Channel, Ground truth, Workdir, …) and `docs/adr/` for the design
decisions behind the architecture.

## Architecture

```
anariCts  (C++ tool)                    renders + scores; writes results/ and ground_truth/
    ↓ loads at runtime                   sidecar JSON + PNGs under a --workdir
ANARI device (helide, or a candidate)
    ↓ files only (ADR-0001)
ctsReport.py  (Python)                   reads the results tree; report + device diff
```

The C++ tool is the single source of image metrics (SSIM, PSNR); Python never
opens a device and never re-computes a metric (ADR-0004). The two communicate
only through the per-Case sidecar files in the workdir (ADR-0003).

## Building

The tool builds as part of the SDK when either `BUILD_CTS` or `BUILD_TESTING` is
on, and needs a device to test against (helide is the reference):

```bash
cmake -DBUILD_CTS=ON -DBUILD_HELIDE_DEVICE=ON ..
cmake --build . -j
```

The glTF asset tests are gated separately on `CTS_ENABLE_GLTF` (decoupled from
`BUILD_CTS`, since they need only the glTF loader, not the tool itself):

```bash
cmake -DBUILD_CTS=ON -DBUILD_HELIDE_DEVICE=ON -DCTS_ENABLE_GLTF=ON ..
```

That registers one Test per asset under `cts/gltf-Sample-Assets/` (override the
location at runtime with `ANARI_CTS_GLTF_ASSETS`); it is a no-op when the assets
directory is absent. Texture-heavy assets need the optional encodings the SDK's
glTF loader supports — build with `-DUSE_DRACO=ON -DUSE_KTX=ON -DUSE_WEBP=ON`
for full coverage; without them, expect benign "Could not decode image" warnings
(those Cases still pass, since ground truth and candidate are both rendered by
the same device).

The Python reporting layer needs only Python 3.9+, and `reportlab` *only* if you
want a PDF — the text summary and the device diff have no third-party
dependencies. See [pyproject.toml](pyproject.toml).

## Usage

```
anariCts <command> [options]

commands:
  list                       list tests, descriptions, and case counts
  generate [options]         render ground truth with the reference device
  run <device> [options]     render + score a candidate device against ground truth
  query-features <device>    print the device's supported extensions
  query-metadata [options]   print catalog metadata as JSON
  check-properties <device>  report which tests the device can run vs. will skip

options:
  --filter <pattern>   select a slice of the catalog (substring or glob over <category>/<test>)
  --workdir <dir>      run root holding results/ ground_truth/ assets/ (default: cts_workdir)
  --device <lib>       reference device library for generate (default: helide)
  --width <n>          render width (default: 256)
  --height <n>         render height (default: 256)
  --stdin              read newline-separated filter patterns from stdin (run)
  --verbose            print ANARI warnings
```

The library being tested must be loadable at runtime (on its
`ANARI_LIBRARY`/`LD_LIBRARY_PATH`), exactly as for any ANARI application.

### Typical flow

Ground truth is generated on demand and never committed (ADR-0005), so the first
step is always to generate it with the reference device, then run the candidate:

```bash
# 1. Generate ground truth for the whole catalog with the reference device.
anariCts generate --workdir myrun

# 2. Render + score a candidate device against it.
anariCts run helide --workdir myrun

# 3. Summarize the run (optionally as a PDF).
python cts/ctsReport.py report myrun
python cts/ctsReport.py report myrun --pdf myrun.pdf
python cts/ctsReport.py report myrun --all --pdf myrun-all.pdf
```

`run` exits non-zero if any Case failed, so it drops into CI directly.

### Running a slice

`--filter` takes a substring or glob (`*`, `?`) matched case-insensitively over
each test's `<category>/<test>` id:

```bash
anariCts list --filter geometry          # see what a filter selects
anariCts generate --filter geometry/sphere --workdir myrun
anariCts run helide --filter "light/*" --workdir myrun
```

`run --stdin` reads one filter pattern per line, accumulating into the same
workdir — useful for an interactively chosen subset:

```bash
printf 'geometry/sphere\nlight/directional\n' | anariCts run helide --stdin --workdir myrun
```

Because results are per-Case sidecars, running subsets at different times
accumulates in one workdir without clobbering (ADR-0003).

### Introspecting a device

```bash
anariCts query-features helide            # extensions the device reports
anariCts check-properties helide          # per test: [run ] or [skip] + missing features
anariCts query-metadata --filter frame    # descriptions and catalog metadata as JSON
```

`check-properties` is the quick way to see why a Test will be skipped on a
device — a Test is skipped when the device is missing any of its required
extensions.

## Reporting and device diff

`ctsReport.py` reads a workdir's sidecar tree; it never renders.

```bash
# Summarize one run (per-category pass/fail/skip, plus failing Cases).
python cts/ctsReport.py report myrun
python cts/ctsReport.py report myrun --pdf myrun.pdf   # also embed failures in a PDF
python cts/ctsReport.py report myrun --all --pdf myrun-all.pdf  # embed every Case

# Compare two candidates, each already run against the same ground truth.
python cts/ctsReport.py diff runA runB
python cts/ctsReport.py diff runA runB --json
```

A device diff is manifest arithmetic over the two sidecar trees — verdict
differences, per-channel metric deltas, and cases present in only one run. It
compares each device against the common ground truth; it does not re-compare
pixels between the two devices (ADR-0004). `report` exits non-zero if any Case
failed; `diff` exits non-zero if the two runs differ. Rendering duration is
intentionally excluded from this semantic comparison, so timing-only changes do
not produce differences or a non-zero exit status.

By default, report details are limited to failed Cases. Pass `--all` to itemize
passed, failed, and skipped Cases. Itemized PDF Cases include the Test's
human-readable description; rendered Cases also include their metric scores and
available candidate and ground-truth images. Text reports include the
description under every itemized Case as well.

## Comparison metrics

Each rendered Channel is compared against its ground-truth image with two
metrics, ported faithfully from the former scikit-image pipeline so the
historical thresholds keep their meaning:

- **SSIM** (structural similarity), default threshold `0.70` — compares image
  structure rather than absolute pixel values, so it tolerates shading
  differences while catching structural ones.
- **PSNR** (peak signal-to-noise ratio, dB), default threshold `20.0`.

Both composite alpha over a white background and use a data range of 255. The
default thresholds can be overridden per Test (and per Channel) in the catalog;
a Case passes only when every metric on every Channel clears its threshold.

## Extending the catalog

Tests are authored directly in C++ (ADR-0002), one per-category file under
`src/anari_test_scenes/cts/tests/`. A Test is declared with the `makeTest`
fluent builder: a `build()` that authors an ANARI world with normal ANARI C++
calls plus the focused construction helpers (`GeometryLayout.h`,
`GeometryBuilder.h`, the remaining `*Builder.h` modules, `TextureGenerator`,
and `ColorPalette`), a short human-readable `description()`, its axes
(`permute`/`variant`), required features, thresholds, and channels. For example:

```cpp
makeTest("geometry", "sphere")
    .description("Checks sphere counts and equivalent soup and indexed layouts.")
    .build([](BuildContext &ctx) { /* author + return an anari::World */ })
    .permute("primitiveCount", {1, 16})   // distinct ground truth per value
    .variant("primitiveMode", {"soup", "indexed"})  // shared ground truth
    .requireFeature("ANARI_KHR_GEOMETRY_SPHERE")
    .registerInto(catalog);
```

The harness lives in `src/anari_test_scenes/cts/` (`Axis`, `Case`, `Catalog`,
`Expansion`, `BuildContext`, `Runner`, `Metrics`, `Workdir`, `Sidecar`, …); the
CLI entry point is `cts/src/main.cpp`, which links the internal static
`anari_cts_core` target. See [AGENTS.md](AGENTS.md) for a map of the source.
