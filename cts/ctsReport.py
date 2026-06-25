#!/usr/bin/env python3
# Copyright 2021-2026 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

"""CTS reporting and device-diff, driven entirely by the per-Case sidecar tree.

The C++ `cts` runner renders and scores every Case and writes a per-Case sidecar
JSON next to its images (ADR-0003). This module is the Python reporting layer
(ADR-0001/0004): it only ever *reads* that tree — it never opens an ANARI device
and never re-computes image metrics.

Commands:
  report <workdir> [--pdf report.pdf]   summarize one run; optional PDF.
  diff <workdir_a> <workdir_b> [--json] compare two candidates' results.

The sidecar schema is versioned (schemaVersion); this reader targets version 1
and warns on a mismatch.
"""

import argparse
import json
import sys
from pathlib import Path

SCHEMA_VERSION = 1

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
    for _, data in iter_sidecars(workdir):
        key = f"{data.get('category','')}/{data.get('test','')}/{data.get('case','')}"
        out[key] = data
    return out


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


def write_text_summary(workdir, results, out=sys.stdout):
    s = summarize(results)
    print(f"CTS report: {workdir}", file=out)
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
    if s["failures"]:
        print("  failed cases:", file=out)
        for key in s["failures"]:
            data = results[key]
            scores = []
            for ch in data.get("channels", []):
                vals = ", ".join(
                    f"{m}={ch['metrics'].get(m)}" for m in METRICS if m in ch.get("metrics", {})
                )
                scores.append(f"{ch.get('channel')}({vals})")
            print(f"    {key}  {'; '.join(scores)}", file=out)
    return s


# --- Device diff (ADR-0004) --------------------------------------------------


def diff_results(results_a, results_b):
    """Compare two candidates' sidecar trees.

    Each is scored against the same ground truth, so a fair comparison is the
    arithmetic of their sidecars: verdict differences, per-channel metric deltas
    (b - a), timing deltas, and cases present in only one run. This never
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
    if not (
        diff["only_in_a"] or diff["only_in_b"] or diff["verdict_changed"] or metric_changes
    ):
        print("  no differences", file=out)


# --- PDF report (optional; requires reportlab) -------------------------------


def generate_pdf(workdir, results, out_path):
    """Render a PDF report from the sidecars. Embeds result/ground-truth images
    for failed cases. Requires reportlab."""
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
    story = []
    s = summarize(results)

    story.append(Paragraph(f"CTS Report: {root.name}", styles["Heading1"]))
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

    failed = [results[k] for k in s["failures"]]
    if failed:
        story.append(PageBreak())
        story.append(Paragraph("Failed cases", styles["Heading2"]))
        for data in failed:
            story.append(
                Paragraph(
                    f"{data['category']}/{data['test']}/{data['case']}",
                    styles["Heading3"],
                )
            )
            for ch in data.get("channels", []):
                if ch.get("passed"):
                    continue
                metrics = ", ".join(
                    f"{m}={ch['metrics'].get(m)}" for m in METRICS if m in ch.get("metrics", {})
                )
                story.append(Paragraph(f"{ch.get('channel')}: {metrics}", styles["Normal"]))
                imgs = []
                for label in ("resultImage", "groundTruthImage"):
                    p = root / ch.get(label, "")
                    if p.is_file():
                        imgs.append(load_image(p))
                if imgs:
                    story.append(Table([imgs], hAlign="LEFT"))
            story.append(Spacer(1, 12))

    SimpleDocTemplate(str(out_path), pagesize=pagesizes.A4).build(story)
    print(f"wrote {out_path}")
    return True


# --- CLI ---------------------------------------------------------------------


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    p_report = sub.add_parser("report", help="summarize one run's sidecar tree")
    p_report.add_argument("workdir")
    p_report.add_argument("--pdf", metavar="PATH", help="also write a PDF report")

    p_diff = sub.add_parser("diff", help="compare two runs' sidecar trees")
    p_diff.add_argument("workdir_a")
    p_diff.add_argument("workdir_b")
    p_diff.add_argument("--json", action="store_true", help="emit JSON instead of text")

    args = parser.parse_args(argv)

    if args.command == "report":
        results = load_results(args.workdir)
        s = write_text_summary(args.workdir, results)
        if args.pdf:
            generate_pdf(args.workdir, results, Path(args.pdf))
        return 1 if s["failed"] else 0

    if args.command == "diff":
        ra = load_results(args.workdir_a)
        rb = load_results(args.workdir_b)
        diff = diff_results(ra, rb)
        if args.json:
            json.dump(diff, sys.stdout, indent=2)
            sys.stdout.write("\n")
        else:
            write_text_diff(args.workdir_a, args.workdir_b, diff)
        changed = bool(
            diff["only_in_a"]
            or diff["only_in_b"]
            or diff["verdict_changed"]
        )
        return 1 if changed else 0

    return 2


if __name__ == "__main__":
    sys.exit(main())
