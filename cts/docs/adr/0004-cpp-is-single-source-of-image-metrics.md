# C++ is the single source of image metrics; device-vs-device diff is manifest arithmetic

All image comparison metrics (SSIM, PSNR, …) live only in the C++ runner, which
scores every Candidate Case against the same Ground truth and records the result
in the sidecar. Python's "compare two devices" feature does not re-compare
pixels: it diffs the two devices' sidecars (pass/fail differences, per-channel
metric deltas, timing, skip/feature differences). There is exactly one metric
implementation.

We chose this over having Python re-implement SSIM/PSNR (which would create a
second metric implementation that silently drifts from the C++ one — the exact
problem this revamp removes) and over adding a `cts compare imgA imgB` primitive
that Python orchestrates per image (more C++ surface and Python driving
subprocesses).

Consequences: device A and device B can only be compared *through* their scores
against a common Ground truth (A-vs-GT and B-vs-GT are known). A direct A-vs-B
pixel comparison for Cases that have no Ground truth is not supported. If that
need ever arises, the chosen escape hatch is a `cts compare` subcommand reusing
the same C++ metric code — never a parallel Python implementation.
