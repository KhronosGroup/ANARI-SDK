#!/usr/bin/env python3
# Copyright 2021-2026 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

"""CTS reporting and device-diff, driven entirely by the per-Case sidecar tree.

The C++ `cts` runner renders and scores every Case and writes a per-Case sidecar
JSON next to its images (ADR-0003). This module is the Python reporting layer
(ADR-0001/0004): it only ever *reads* that tree — it never opens an ANARI device
and never re-computes image metrics.

Commands:
  report <workdir> [--all] [--pdf report.pdf]
                                        summarize one run; optional PDF.
  diff <workdir_a> <workdir_b> [--json] compare two candidates' results.

The sidecar schema is versioned (schemaVersion); this reader targets version 2
(which added the producing-device identity) and warns on a mismatch.
"""

import argparse
import html
import json
import sys
from pathlib import Path

SCHEMA_VERSION = 2

# Metrics shown in summaries/diffs, in display order.
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


def channel_metric(data, channel, metric):
    """A channel's metric score, or None if absent/non-finite."""
    for ch in data.get("channels", []):
        if ch.get("channel") == channel:
            return ch.get("metrics", {}).get(metric)
    return None


def report_case_keys(results, include_all=False):
    """Case keys to itemize: every Case, or only failures by default."""
    if include_all:
        return sorted(results)
    return summarize(results)["failures"]


def write_text_summary(workdir, results, out=sys.stdout, include_all=False):
    s = summarize(results)
    print(f"CTS report: {workdir}", file=out)
    device = run_device(results)
    if device:
        print(
            f"  device: {device.get('library')} "
            f"({device.get('device', 'default')}/"
            f"{device.get('renderer', 'default')})",
            file=out,
        )
    print(
        f"  {s['total']} cases: {s['passed']} passed, {s['failed']} failed, "
        f"{s['skipped']} skipped",
        file=out,
    )
    print("  by category:", file=out)
    for cat, c in sorted(s["categories"].items()):
        print(
            f"    {cat:12s} {c['passed']:4d} passed  {c['failed']:4d} failed  "
            f"{c['skipped']:4d} skipped",
            file=out,
        )
    case_keys = report_case_keys(results, include_all)
    if case_keys:
        heading = "all cases" if include_all else "failed cases"
        print(f"  {heading}:", file=out)
        for key in case_keys:
            data = results[key]
            scores = []
            for ch in data.get("channels", []):
                vals = ", ".join(
                    f"{m}={ch['metrics'].get(m)}" for m in METRICS if m in ch.get("metrics", {})
                )
                scores.append(f"{ch.get('channel')}({vals})")
            # Cases with no channel scores (behavioral, or a failed render)
            # carry their reason in detail or skipReason instead.
            reason = data.get("detail") or data.get("skipReason", "")
            summary_text = "; ".join(scores) if scores else reason
            verdict = f" [{data.get('verdict', 'skipped')}]" if include_all else ""
            print(f"    {key}{verdict}  {summary_text}", file=out)
            description = data.get("description")
            if isinstance(description, str) and description:
                print(f"      Description: {description}", file=out)
    return s


# --- Device diff (ADR-0004) --------------------------------------------------


def diff_results(results_a, results_b):
    """Compare two candidates' sidecar trees.

    Each is scored against the same ground truth, so a fair comparison is the
    arithmetic of their sidecars: verdict differences, per-channel metric deltas
    (b - a), and cases present in only one run. Duration deltas accompany Cases
    with semantic changes but do not create changes on their own. This never
    re-compares pixels.
    """
    keys = sorted(set(results_a) | set(results_b))
    diff = {"only_in_a": [], "only_in_b": [], "verdict_changed": [], "cases": []}
    for key in keys:
        a = results_a.get(key)
        b = results_b.get(key)
        if a is None:
            diff["only_in_b"].append(key)
            continue
        if b is None:
            diff["only_in_a"].append(key)
            continue
        va, vb = a.get("verdict"), b.get("verdict")
        entry = {"case": key, "verdict_a": va, "verdict_b": vb, "channels": []}
        if va != vb:
            diff["verdict_changed"].append(entry)
        for metric in METRICS:
            for ch in {c.get("channel") for c in a.get("channels", [])} | {
                c.get("channel") for c in b.get("channels", [])
            }:
                ma = channel_metric(a, ch, metric)
                mb = channel_metric(b, ch, metric)
                delta = (mb - ma) if (isinstance(ma, (int, float)) and isinstance(mb, (int, float))) else None
                if ma != mb:
                    entry["channels"].append(
                        {"channel": ch, "metric": metric, "a": ma, "b": mb, "delta": delta}
                    )
        ta, tb = a.get("durationMs", 0.0), b.get("durationMs", 0.0)
        entry["durationDeltaMs"] = tb - ta
        if entry["channels"] or va != vb:
            diff["cases"].append(entry)
    return diff


def has_differences(diff):
    """Whether a device diff contains any semantic result differences."""
    return bool(
        diff["only_in_a"]
        or diff["only_in_b"]
        or diff["verdict_changed"]
        or any(entry["channels"] for entry in diff["cases"])
    )


def write_text_diff(name_a, name_b, diff, out=sys.stdout):
    print(f"CTS device diff: A={name_a}  B={name_b}", file=out)
    if diff["only_in_a"]:
        print(f"  only in A ({len(diff['only_in_a'])}):", file=out)
        for k in diff["only_in_a"]:
            print(f"    {k}", file=out)
    if diff["only_in_b"]:
        print(f"  only in B ({len(diff['only_in_b'])}):", file=out)
        for k in diff["only_in_b"]:
            print(f"    {k}", file=out)
    if diff["verdict_changed"]:
        print(f"  verdict changed ({len(diff['verdict_changed'])}):", file=out)
        for e in diff["verdict_changed"]:
            print(f"    {e['case']}: {e['verdict_a']} -> {e['verdict_b']}", file=out)
    metric_changes = [e for e in diff["cases"] if e["channels"]]
    if metric_changes:
        print(f"  metric deltas ({len(metric_changes)} cases):", file=out)
        for e in metric_changes:
            for c in e["channels"]:
                d = c["delta"]
                ds = f"{d:+.4f}" if isinstance(d, (int, float)) else "n/a"
                print(
                    f"    {e['case']} [{c['channel']}/{c['metric']}] "
                    f"{c['a']} -> {c['b']} ({ds})",
                    file=out,
                )
    if not has_differences(diff):
        print("  no differences", file=out)


# --- PDF report (optional; requires reportlab) -------------------------------


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
            "(text summary still works without --pdf)",
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

    p_report = sub.add_parser("report", help="summarize one run's sidecar tree")
    p_report.add_argument("workdir")
    p_report.add_argument("--pdf", metavar="PATH", help="also write a PDF report")
    p_report.add_argument(
        "--all",
        action="store_true",
        help="itemize every case (PDF default: failed cases only)",
    )

    p_diff = sub.add_parser("diff", help="compare two runs' sidecar trees")
    p_diff.add_argument("workdir_a")
    p_diff.add_argument("workdir_b")
    p_diff.add_argument("--json", action="store_true", help="emit JSON instead of text")

    args = parser.parse_args(argv)

    if args.command == "report":
        results = load_results(args.workdir)
        s = write_text_summary(args.workdir, results, include_all=args.all)
        if args.pdf:
            generate_pdf(
                args.workdir, results, Path(args.pdf), include_all=args.all
            )
        return 1 if s["failed"] else 0

    if args.command == "diff":
        ra = load_results(args.workdir_a)
        rb = load_results(args.workdir_b)
        diff = diff_results(ra, rb)
        # Label the two runs by their producing device (schema v2), falling
        # back to the workdir name for pre-v2 sidecars.
        label_a = device_label(ra, args.workdir_a)
        label_b = device_label(rb, args.workdir_b)
        diff["device_a"] = label_a
        diff["device_b"] = label_b
        if args.json:
            json.dump(diff, sys.stdout, indent=2)
            sys.stdout.write("\n")
        else:
            write_text_diff(label_a, label_b, diff)
        return 1 if has_differences(diff) else 0

    return 2


if __name__ == "__main__":
    sys.exit(main())
