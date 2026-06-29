# CTS Features

A capability-by-capability reference for the CTS. It complements
[README.md](README.md) (setup + command reference) and [CONTEXT.md](CONTEXT.md)
(vocabulary). Every capability below is delivered by the `anariCts` C++ tool, except
reporting and device-diff, which are the Python layer reading the results tree
(`docs/adr/0001`).

- [Render and score test scenes](#render-and-score-test-scenes)
- [Comparison metrics](#comparison-metrics)
- [Query object/parameter metadata](#query-objectparameter-metadata)
- [Check which tests a device can run](#check-which-tests-a-device-can-run)
- [List the extensions a device implements](#list-the-extensions-a-device-implements)
- [Aggregate results: report and device diff](#aggregate-results-report-and-device-diff)

## Render and score test scenes

The catalog is a set of C++ Tests, each expanding into one or more Cases. The
reference device (helide) renders the ground truth; the candidate device renders
the same Cases and each Channel is scored against ground truth.

```bash
anariCts generate --workdir myrun     # ground truth from the reference device
anariCts run helide --workdir myrun   # render + score a candidate
```

Ground truth is generated on demand into `myrun/ground_truth/` and is never
committed (`docs/adr/0005`), so it always matches the current scene code.
Rendered candidate images and a per-Case sidecar land in `myrun/results/`,
mirroring the catalog hierarchy `<category>/<test>/<case>`. A Case is rendered
once per Channel it declares (color, depth, albedo, normal, primitive/object/
instance id); each Channel is compared independently.

A handful of Tests verify *behavior* the render-and-compare path can't express
(the frame-completion callback firing, progressive accumulation converging).
Those use the runner's behavior hook and record a pass/fail with a detail note
instead of comparing images.

## Comparison metrics

Each Channel is compared against its ground-truth image with two metrics, ported
faithfully from the former scikit-image pipeline so the historical thresholds
keep their meaning. C++ is the single source of these metrics (`docs/adr/0004`);
the Python layer never re-computes them.

- **SSIM** (structural similarity), default threshold `0.70`. SSIM mimics human
  perception and compares the *structure* of two images rather than their
  absolute pixel values, so it tolerates differences in lighting/shading while
  still catching structural differences. (If two images differ too much, SSIM
  also drops, so it is not blind to shading.)
- **PSNR** (peak signal-to-noise ratio, dB), default threshold `20.0`.

Both composite the alpha channel over a white background before comparing and
use a data range of 255. The **depth** Channel is rendered to a grayscale image
(distance from the camera, per the ANARI spec) with a fixed, scene-derived scale
so it is deterministic across devices; depth images are independent of shading
and exercise only the primitives.

Thresholds are configurable per Test, and per Channel within a Test (e.g. a
stricter bar on depth than on color). A Case passes only when every metric on
every Channel clears its threshold.

## Query object/parameter metadata

`query-metadata` prints the catalog's machine-readable metadata as JSON — each
Test's id, category, axes, required features, channels, thresholds, and Case
count. It opens no device, so it works without any library present.

```bash
anariCts query-metadata --filter frame
```

```json
[
  {
    "id": "frame/frame_depth_channel",
    "category": "frame",
    "name": "frame_depth_channel",
    "caseCount": 1,
    "channels": ["depth"],
    "requiredFeatures": ["ANARI_KHR_GEOMETRY_TRIANGLE"],
    "axes": [],
    "thresholds": {},
    "simplified": false
  }
]
```

## Check which tests a device can run

A Test is skipped when the device is missing any of its required extensions.
`check-properties` loads a device and reports, per Test, whether it is runnable
or will be skipped and which features are missing:

```bash
anariCts check-properties helide --filter frame
```

```
[run ] frame/frame_color_channel
[run ] frame/frame_depth_channel
[skip] frame/frame_albedo_channel  missing: ANARI_KHR_FRAME_CHANNEL_ALBEDO
[skip] frame/frame_normal_channel  missing: ANARI_KHR_FRAME_CHANNEL_NORMAL
[run ] frame/frame_completion_callback
[skip] frame/progressive_rendering  missing: ANARI_KHR_PROGRESSIVE_RENDERING
...
12 runnable, 4 skipped on 'helide'
```

## List the extensions a device implements

`query-features` loads a device and prints the extensions it reports via
`anariGetDeviceExtensions`, one per line:

```bash
anariCts query-features helide
```

```
ANARI_KHR_CAMERA_PERSPECTIVE
ANARI_KHR_FRAME_COMPLETION_CALLBACK
ANARI_KHR_GEOMETRY_TRIANGLE
ANARI_KHR_MATERIAL_MATTE
...
```

## Aggregate results: report and device diff

`ctsReport.py` reads a workdir's sidecar tree and never renders.

```bash
python cts/ctsReport.py report myrun            # text summary
python cts/ctsReport.py report myrun --pdf out.pdf
python cts/ctsReport.py diff runA runB          # compare two candidates
python cts/ctsReport.py diff runA runB --json
```

`report` prints overall and per-category pass/fail/skip counts and lists failing
Cases (with their per-Channel metric scores, or the behavioral detail note). The
optional PDF additionally embeds the candidate vs. ground-truth images for each
failed Case.

A **device diff** is manifest arithmetic over two sidecar trees that were each
scored against the same ground truth: verdict differences, per-Channel metric
deltas, and Cases present in only one run. Rendering duration is intentionally
excluded from semantic equality, so timing-only changes do not make repeated
runs differ. It does not re-compare pixels between the two devices
(`docs/adr/0004`).
