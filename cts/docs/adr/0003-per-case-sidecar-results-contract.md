# Results are per-case sidecar JSON files; the results tree is the C++/Python contract

The C++ runner writes, for each Case, its rendered image(s) plus a sidecar
`<case>.json` next to them under `results/<category>/<test>/`. The sidecar holds
the Test's human-readable description when available, axis values, per-channel
metric scores and thresholds, the pass/fail verdict, any skip reason, timing,
relative image paths, and the identity of the device that produced the run (the
`device` object: library / device / renderer, schema v2) so a two-device diff
labels runs by device rather than by workdir name. The Python layer consumes
results purely by globbing this tree — it never renders and never opens a
device.

We chose per-case sidecars over a single run-level manifest because the suite is
explicitly run in *subsets* (filtered or interactively selected): running
`geometry` now and `light` later must accumulate in one results tree without
clobbering, and a crash mid-run should lose only the in-flight Case. A single
manifest would be overwritten by subset runs and corruptible by a crash. We
rejected the sidecars-plus-merged-index hybrid to avoid keeping two
representations coherent; aggregation is cheap to do on demand by globbing.

Consequences: "the manifest" is the whole results tree, not one file. Aggregation
(for a report or a device diff) is a glob-and-merge step. The sidecar schema is a
versioned cross-language contract and changes to it must keep the Python reader
in step.
