# Reporting is a C++ responsibility; Python retains only PDF

The text summary and interactive HTML report are produced by the `anariCts`
tool itself (the `report` command) reading the results tree; the Python layer
(`ctsReport.py`) is reduced to the one output that would be expensive to
reproduce in C++ — the `reportlab` PDF. This makes the common reporting path
python-free: a single binary generates and reports on a run.

This reverses the earlier framing (ADR-0001) that Python is the reader and
presenter. The files-only boundary still holds: the C++ report reads *its own
output tree* (the per-Case sidecars, ADR-0003) and never loads a device, so
reporting stays a pure, offline transform of the results tree — the same
contract Python consumes. The split is drawn by cost, not by language: text and
HTML are trivial string work in C++ (nlohmann::json is already vendored for
reading), whereas a C++ PDF would pull in a heavyweight dependency for a
rarely-needed output that a browser's "Print to PDF" over the HTML already
covers.

## Consequences

- Aggregation exists in two places (C++ `Report` for text/HTML, Python for
  PDF). Both are independent readers of the one contract (the results tree),
  which ADR-0003 explicitly allows; the sidecar schema keeps them consistent.
- A two-run device diff could follow the same pattern (read both trees, compare
  sidecars) but is not implemented yet — reporting is scoped to a single run.
- The list of reported metrics now lives in C++ (`kReportMetrics` in
  `Report.h`); the Python PDF keeps its own copy for its own output.
- The HTML report is self-contained (inline CSS/JS, no CDN) and can embed images
  as base64 (`--embed`) for a portable single file; by default it references the
  PNGs already in the workdir.
