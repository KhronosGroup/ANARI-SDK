#!/usr/bin/env python3
# Copyright 2021-2026 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

"""CTS PDF report, driven entirely by the per-Case sidecar tree.

Reporting is a C++ responsibility (ADR-0008): `anariCts report` emits the text
summary and the interactive HTML report. This module is retained only for the
one output not worth reproducing in C++ — the reportlab PDF. Like the C++
reporter, it only ever *reads* the results tree the runner writes
(ADR-0001/0003/0004); it never opens an ANARI device and never re-computes image
metrics.

Command:
  report <workdir> --pdf report.pdf [--all]   write a PDF from one run.

The sidecar schema is versioned (schemaVersion); this reader targets version 2
(which added the producing-device identity) and warns on a mismatch.
"""

import argparse
import html
import json
import sys
from pathlib import Path

SCHEMA_VERSION = 2

# Metrics shown in the PDF, in display order. The C++ reporters keep their own
# copy (kReportMetrics); both are independent readers of the one contract.
METRICS = ["ssim", "psnr"]


# --- Sidecar loading ---------------------------------------------------------


def iter_sidecars(workdir):
    """Yield (path, parsed-sidecar-dict) for every sidecar under workdir/results.

    Skips files that are not valid sidecars; warns once per schema mismatch.
    """
    results = Path(workdir) / "results"
    if not results.is_dir():
        return
    for path in sorted(results.rglob("*.json")):
        try:
            with open(path) as f:
                data = json.load(f)
        except (OSError, json.JSONDecodeError) as e:
            print(f"warning: skipping unreadable sidecar {path}: {e}", file=sys.stderr)
            continue
        if not isinstance(data, dict) or "verdict" not in data:
            continue  # not a sidecar
        version = data.get("schemaVersion")
        if version != SCHEMA_VERSION:
            print(
                f"warning: {path} has schemaVersion {version}, expected "
                f"{SCHEMA_VERSION}; reading best-effort",
                file=sys.stderr,
            )
        yield path, data


def load_results(workdir):
    """All sidecars in a workdir, keyed by 'category/test/case'."""
    out = {}
    sources = {}
    for path, data in iter_sidecars(workdir):
        key = f"{data.get('category','')}/{data.get('test','')}/{data.get('case','')}"
        if key in out:
            print(
                f"warning: duplicate logical Case key {key} in {path}; "
                f"keeping {sources[key]}",
                file=sys.stderr,
            )
            continue
        out[key] = data
        sources[key] = path
    return out


def run_device(results):
    """The consistent device identity in a run's sidecars, or None.

    Schema-v2 sidecars should all name the same device. Mixed identities are
    diagnosed instead of labeling the run with whichever sidecar was read first.
    """
    devices = {}
    for data in results.values():
        device = data.get("device")
        if isinstance(device, dict) and device.get("library"):
            identity = (
                device["library"],
                device.get("device", "default"),
                device.get("renderer", "default"),
            )
            devices.setdefault(identity, device)
    if len(devices) > 1:
        labels = ", ".join(
            f"{library} ({device}/{renderer})"
            for library, device, renderer in sorted(devices)
        )
        print(f"warning: mixed device identities in one run: {labels}", file=sys.stderr)
        return None
    return next(iter(devices.values()), None)


def device_label(results, fallback):
    """A human label for a run: its device identity, or the workdir name.

    For a suite whose purpose is comparing devices, the device is the right
    label; the workdir name is only a fallback for pre-v2 sidecars.
    """
    device = run_device(results)
    if not device:
        return fallback
    return (
        f"{device.get('library')} "
        f"({device.get('device', 'default')}/{device.get('renderer', 'default')})"
    )


# --- Aggregation -------------------------------------------------------------


def summarize(results):
    """Counts overall and per category, plus the list of failed case keys."""
    summary = {
        "total": 0,
        "passed": 0,
        "failed": 0,
        "skipped": 0,
        "categories": {},
        "failures": [],
    }
    for key, data in sorted(results.items()):
        verdict = data.get("verdict", "skipped")
        summary["total"] += 1
        summary[verdict] = summary.get(verdict, 0) + 1
        cat = data.get("category", "")
        c = summary["categories"].setdefault(
            cat, {"passed": 0, "failed": 0, "skipped": 0}
        )
        c[verdict] = c.get(verdict, 0) + 1
        if verdict == "failed":
            summary["failures"].append(key)
    return summary


def report_case_keys(results, include_all=False):
    """Case keys to itemize: every Case, or only failures by default."""
    if include_all:
        return sorted(results)
    return summarize(results)["failures"]


# --- PDF report (requires reportlab) -----------------------------------------


def generate_pdf(workdir, results, out_path, *, include_all=False):
    """Render a PDF report from the sidecars. Embeds result/ground-truth images
    for failed cases, or every case when include_all is true. Requires
    reportlab."""
    try:
        from reportlab.lib import colors, pagesizes, utils
        from reportlab.lib.styles import getSampleStyleSheet
        from reportlab.platypus import (
            Image,
            PageBreak,
            Paragraph,
            SimpleDocTemplate,
            Spacer,
            Table,
            TableStyle,
        )
    except ImportError:
        print(
            "error: reportlab is not installed; cannot write a PDF "
            "(anariCts report gives text/HTML without it)",
            file=sys.stderr,
        )
        return False

    root = Path(workdir)
    styles = getSampleStyleSheet()
    doc = SimpleDocTemplate(str(out_path), pagesize=pagesizes.A4)
    story = []
    s = summarize(results)

    story.append(Paragraph(f"CTS Report: {root.name}", styles["Heading1"]))
    device = run_device(results)
    if device:
        story.append(
            Paragraph(
                f"Device: {device.get('library')} "
                f"({device.get('device', 'default')}/"
                f"{device.get('renderer', 'default')})",
                styles["Normal"],
            )
        )
    story.append(
        Paragraph(
            f"{s['total']} cases — {s['passed']} passed, {s['failed']} failed, "
            f"{s['skipped']} skipped",
            styles["Normal"],
        )
    )
    story.append(Spacer(1, 12))

    rows = [["Category", "Passed", "Failed", "Skipped"]]
    for cat, c in sorted(s["categories"].items()):
        rows.append([cat, str(c["passed"]), str(c["failed"]), str(c["skipped"])])
    table = Table(rows, hAlign="LEFT")
    table.setStyle(TableStyle([("GRID", (0, 0), (-1, -1), 0.25, colors.black)]))
    story.append(table)

    def load_image(path, width=240):
        reader = utils.ImageReader(str(path))
        iw, ih = reader.getSize()
        aspect = ih / float(iw)
        return Image(str(path), width=width, height=width * aspect)

    reported_cases = [results[k] for k in report_case_keys(results, include_all)]
    if reported_cases:
        story.append(PageBreak())
        heading = "All cases" if include_all else "Failed cases"
        story.append(Paragraph(heading, styles["Heading2"]))
        for data in reported_cases:
            story.append(
                Paragraph(
                    f"{data['category']}/{data['test']}/{data['case']}",
                    styles["Heading3"],
                )
            )
            description = data.get("description")
            if isinstance(description, str) and description:
                story.append(
                    Paragraph(
                        f"Description: {html.escape(description)}",
                        styles["Normal"],
                    )
                )
            if include_all:
                verdict = data.get("verdict", "skipped").capitalize()
                story.append(Paragraph(f"Verdict: {verdict}", styles["Normal"]))
            # Cases with no channels (behavioral, or a failed render) carry a
            # reason note instead of channel images.
            if not data.get("channels"):
                reason = data.get("detail") or data.get("skipReason", "")
                if reason:
                    story.append(Paragraph(reason, styles["Normal"]))
            for ch in data.get("channels", []):
                if not include_all and ch.get("passed"):
                    continue
                metrics = ", ".join(
                    f"{m}={ch['metrics'].get(m)}" for m in METRICS if m in ch.get("metrics", {})
                )
                story.append(Paragraph(f"{ch.get('channel')}: {metrics}", styles["Normal"]))
                image_paths = []
                # Result, ground truth, then the debug images (diff + mask) when
                # the runner emitted them (schema v2).
                for label in (
                    "resultImage",
                    "groundTruthImage",
                    "diffImage",
                    "thresholdImage",
                ):
                    rel = ch.get(label, "")
                    if rel and (root / rel).is_file():
                        image_paths.append(root / rel)
                if image_paths:
                    columns = min(2, len(image_paths))
                    cell_padding = 6
                    image_width = min(
                        240, doc.width / columns - 2 * cell_padding
                    )
                    imgs = [load_image(path, image_width) for path in image_paths]
                    image_rows = [
                        imgs[i : i + columns]
                        for i in range(0, len(imgs), columns)
                    ]
                    image_table = Table(image_rows, hAlign="LEFT")
                    image_table.setStyle(
                        TableStyle(
                            [
                                (
                                    "LEFTPADDING",
                                    (0, 0),
                                    (-1, -1),
                                    cell_padding,
                                ),
                                (
                                    "RIGHTPADDING",
                                    (0, 0),
                                    (-1, -1),
                                    cell_padding,
                                ),
                            ]
                        )
                    )
                    story.append(image_table)
            story.append(Spacer(1, 12))

    doc.build(story)
    print(f"wrote {out_path}")
    return True


# --- CLI ---------------------------------------------------------------------


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    p_report = sub.add_parser("report", help="write a PDF from one run's sidecar tree")
    p_report.add_argument("workdir")
    p_report.add_argument("--pdf", metavar="PATH", required=True, help="PDF output path")
    p_report.add_argument(
        "--all",
        action="store_true",
        help="itemize every case (default: failed cases only)",
    )

    args = parser.parse_args(argv)

    if args.command == "report":
        results = load_results(args.workdir)
        ok = generate_pdf(
            args.workdir, results, Path(args.pdf), include_all=args.all
        )
        if not ok:
            return 2
        return 1 if summarize(results)["failed"] else 0

    return 2


if __name__ == "__main__":
    sys.exit(main())
