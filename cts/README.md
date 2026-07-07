# CTS

The Conformance Test Suite (CTS) validates an ANARI device implementation by
rendering a known battery of scenes and comparing each render against
ground-truth images produced by a reference device (helide). It is a
self-contained C++ command-line tool, `anariCts`, with a thin Python layer
(`ctsReport.py`) retained only for producing a PDF.

It can:

- enumerate the test catalog (`list`, `query-metadata`),
- generate ground-truth images with the reference device (`generate`),
- render and score a candidate device against that ground truth (`run`),
- report which extensions a device implements (`query-features`) and which
  tests it can run vs. will skip (`check-properties`),
- summarize a run as text or an interactive HTML report (`report`) — a PDF is
  available via `ctsReport.py`.

See [CONTEXT.md](CONTEXT.md) for the vocabulary (Test, Axis, Permutation,
Variant, Case, Channel, Ground truth, Workdir, …) and `docs/adr/` for the design
decisions behind the architecture.

## Architecture

```
anariCts  (C++ tool)                    renders + scores; writes results/ and ground_truth/
    │                                    sidecar JSON + PNGs under a --workdir
    ├─ report                            reads the results tree -> text / HTML (ADR-0008)
    └─ files only (ADR-0001)
ctsReport.py  (Python)                   reads the same results tree; retained only for the PDF
```

The C++ tool is the single source of image metrics (SSIM, PSNR); no reader ever
opens a device or re-computes a metric (ADR-0004). Reporting reads the per-Case
sidecar files in the workdir (ADR-0003); the results tree is the only contract.

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

The text summary and HTML report are built into `anariCts` and need nothing
further. The Python layer is needed *only* for a PDF, and then only Python 3.9+
plus `reportlab`. See [pyproject.toml](pyproject.toml).

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
  report <workdir>           summarize a run's results tree (text + optional HTML)

options:
  --filter <pattern>   select a slice of the catalog (substring or glob over <category>/<test>)
  --workdir <dir>      run root holding results/ ground_truth/ assets/ (default: cts_workdir)
  --device <lib>       reference device library for generate (default: helide)
  --width <n>          render width (default: 256)
  --height <n>         render height (default: 256)
  -r, --renderer <subtype>
                       renderer subtype (default: default)
  --ambientRadiance <value>
                       baseline renderer ambientRadiance (default: 1)
  -p, --renderer-param NAME=VALUE
                       set a custom renderer parameter on every rendered case
                       (repeatable; device-agnostic). VALUE parses into the
                       renderer's device-declared type, else inferred as bool/
                       int/float/float-vector/string. e.g. -p ambientSample=4
  --accumulation <n>   progressive color-channel frames when supported (default: 16)
  --no-accumulation    render each channel once
  --denoise            set the renderer denoise parameter
  --stdin              read newline-separated filter patterns from stdin (run)
  --verbose            print ANARI warnings

report options:
  --html <path>        also write an interactive HTML report
  --embed              inline images in the HTML (portable single file; embeds
                       only the initially-shown cases unless combined with --all)
  --all                itemize every case (default: failures only)
```

The library being tested must be loadable at runtime (on its
`ANARI_LIBRARY`/`LD_LIBRARY_PATH`), exactly as for any ANARI application.

### Typical flow

Ground truth is generated on demand and never committed (ADR-0005), so the first
step is always to generate it with the reference device, then run the candidate:

```bash
# 1. Generate ground truth for the whole catalog with the reference device.
anariCts generate --renderer default --ambientRadiance 1 --workdir myrun

# 2. Render + score a candidate device against it.
anariCts run helide --renderer default --ambientRadiance 1 --workdir myrun

# 3. Summarize the run (text, or an interactive HTML report).
anariCts report myrun
anariCts report myrun --html myrun.html
anariCts report myrun --all --embed --html myrun-all.html   # portable single file
python cts/ctsReport.py report myrun --pdf myrun.pdf         # PDF, if you want one
```

`run` exits non-zero if any Case failed, so it drops into CI directly.

Use the same `--renderer` and `--ambientRadiance` values for `generate` and
`run`; they are part of the rendering configuration being compared. The
ambient value is a baseline for ordinary Tests. A renderer Test that explicitly
exercises `ambientRadiance` overrides it with the Case's axis value.

Pass device-specific renderer knobs with `-p/--renderer-param NAME=VALUE`
(repeatable), e.g. `-p ambientSample=4`. It stays device-agnostic: the value is
typed against the renderer subtype's device-declared parameter metadata (falling
back to inference when the device does not report the parameter), so no
device-specific names live in the CTS. Overrides apply over the baseline but
before a renderer Test's own configuration, so such a Test still wins on
conflict. As with `--ambientRadiance`, use the same overrides for `generate` and
`run` when they affect the compared image.

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

## Reporting

`anariCts report` reads a workdir's results tree; it never renders or opens a
device (ADR-0008).

```bash
# Summarize one run (per-category pass/fail/skip, plus failing Cases).
anariCts report myrun
anariCts report myrun --html myrun.html          # interactive HTML alongside the text
anariCts report myrun --all --html myrun-all.html   # itemize every Case
anariCts report myrun --all --embed --html myrun.html  # inline images: one portable file

# A PDF is still available from the Python layer.
python cts/ctsReport.py report myrun --pdf myrun.pdf
```

`report` exits non-zero if any Case failed, so it drops into CI directly.

By default, report details are limited to failed Cases. Pass `--all` to itemize
passed, failed, and skipped Cases. The HTML report always carries every Case's
metadata so its status filter and search work client-side; it defaults to a
"failed first" ordering (with a synthetic Failed section) when the run has
failures, and can switch back to grouping by category. With `--embed`, images
travel inline as base64 for the initially-shown Cases only (add `--all` to embed
everything), so a shared file stays bounded; without it, the HTML references the
PNGs in the workdir. Each channel offers a result/ground-truth A/B flip (click
or toggle) and an interactive difference-mask canvas — a threshold slider paints
the exceeding pixels red, with an "over actual" toggle to see them on a dimmed
render. (Reading diff pixels needs canvas-readable images: works with `--embed`
or when served over http; a referenced `file://` diff falls back to the static
image.)

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
