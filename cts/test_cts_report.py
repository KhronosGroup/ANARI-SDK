#!/usr/bin/env python3
# Copyright 2026 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

import contextlib
import io
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from cts import ctsReport


REPORT_SCRIPT = Path(__file__).with_name("ctsReport.py")


def make_sidecar(
    *,
    category="geometry",
    test="triangle",
    case="default",
    verdict="passed",
    ssim=0.95,
    psnr=31.0,
    duration_ms=10.0,
    device_library="helide",
    description="Checks triangle geometry rendering.",
):
    return {
        "schemaVersion": 2,
        "category": category,
        "test": test,
        "description": description,
        "case": case,
        "device": {
            "library": device_library,
            "device": "default",
            "renderer": "default",
        },
        "verdict": verdict,
        "durationMs": duration_ms,
        "channels": [
            {
                "channel": "color",
                "metrics": {"ssim": ssim, "psnr": psnr},
                "passed": verdict == "passed",
            }
        ],
    }


def write_sidecar(workdir, *, filename=None, **values):
    data = make_sidecar(**values)
    return write_sidecar_data(workdir, data, filename=filename)


def write_sidecar_data(workdir, data, *, filename=None):
    filename = filename or data["case"]
    path = (
        Path(workdir)
        / "results"
        / data["category"]
        / data["test"]
        / f"{filename}.json"
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data), encoding="utf-8")
    return path


def run_diff(run_a, run_b, *args):
    return subprocess.run(
        [sys.executable, REPORT_SCRIPT, "diff", run_a, run_b, *args],
        check=False,
        capture_output=True,
        text=True,
    )


class ReportContractTests(unittest.TestCase):
    def test_mixed_device_identities_are_diagnosed_and_not_mislabeled(self):
        results = {
            "geometry/triangle/a": make_sidecar(
                case="a", device_library="device-a"
            ),
            "geometry/triangle/b": make_sidecar(
                case="b", device_library="device-b"
            ),
        }
        errors = io.StringIO()

        with contextlib.redirect_stderr(errors):
            label = ctsReport.device_label(results, "candidate")

        self.assertEqual(label, "candidate")
        self.assertIn("mixed device identities", errors.getvalue())
        self.assertIn("device-a (default/default)", errors.getvalue())
        self.assertIn("device-b (default/default)", errors.getvalue())

    def test_schema_v2_device_identity_labels_the_run(self):
        sidecar = make_sidecar(device_library="vendor")
        sidecar["device"]["device"] = "production"
        sidecar["device"]["renderer"] = "pathtracer"

        label = ctsReport.device_label(
            {"geometry/triangle/default": sidecar}, "candidate"
        )

        self.assertEqual(label, "vendor (production/pathtracer)")

    def test_duplicate_logical_case_keys_are_diagnosed_without_overwrite(self):
        with tempfile.TemporaryDirectory() as temp:
            workdir = Path(temp) / "candidate"
            write_sidecar(workdir, filename="a", ssim=0.95)
            write_sidecar(workdir, filename="b", ssim=0.90)
            errors = io.StringIO()

            with contextlib.redirect_stderr(errors):
                results = ctsReport.load_results(workdir)

        key = "geometry/triangle/default"
        self.assertIn("duplicate logical Case key", errors.getvalue())
        self.assertEqual(results[key]["channels"][0]["metrics"]["ssim"], 0.95)

    def test_loading_diagnoses_bad_files_and_accepts_absent_optional_fields(self):
        with tempfile.TemporaryDirectory() as temp:
            workdir = Path(temp) / "candidate"
            minimal = make_sidecar(case="minimal")
            for optional in ("device", "description", "durationMs", "channels"):
                minimal.pop(optional)
            write_sidecar_data(workdir, minimal)
            old_schema = make_sidecar(case="old-schema")
            old_schema["schemaVersion"] = 1
            write_sidecar_data(workdir, old_schema)
            malformed = workdir / "results" / "geometry" / "triangle" / "bad.json"
            malformed.write_text("{not json", encoding="utf-8")
            errors = io.StringIO()

            with contextlib.redirect_stderr(errors):
                results = ctsReport.load_results(workdir)

        self.assertEqual(
            set(results),
            {
                "geometry/triangle/minimal",
                "geometry/triangle/old-schema",
            },
        )
        self.assertIn("skipping unreadable sidecar", errors.getvalue())
        self.assertIn("has schemaVersion 1, expected 2", errors.getvalue())

    def test_summary_covers_rendered_skipped_and_behavioral_cases(self):
        failed = make_sidecar(case="failed", verdict="failed")
        skipped = make_sidecar(case="skipped", verdict="skipped")
        skipped["channels"] = []
        skipped["skipReason"] = "missing feature"
        behavioral = make_sidecar(
            category="frame", test="callback", case="behavioral"
        )
        behavioral["channels"] = []
        behavioral["detail"] = "frame completion callback fired"
        results = {
            "geometry/triangle/passed": make_sidecar(case="passed"),
            "geometry/triangle/failed": failed,
            "geometry/triangle/skipped": skipped,
            "frame/callback/behavioral": behavioral,
        }
        output = io.StringIO()

        summary = ctsReport.write_text_summary("candidate", results, output)

        self.assertEqual(summary["total"], 4)
        self.assertEqual(summary["passed"], 2)
        self.assertEqual(summary["failed"], 1)
        self.assertEqual(summary["skipped"], 1)
        self.assertEqual(summary["failures"], ["geometry/triangle/failed"])
        self.assertIn("4 cases: 2 passed, 1 failed, 1 skipped", output.getvalue())

    def test_all_mode_lists_every_case_with_its_verdict(self):
        skipped = make_sidecar(case="skipped", verdict="skipped")
        skipped["channels"] = []
        skipped["skipReason"] = "missing feature"
        results = {
            "geometry/triangle/passed": make_sidecar(case="passed"),
            "geometry/triangle/failed": make_sidecar(
                case="failed", verdict="failed"
            ),
            "geometry/triangle/skipped": skipped,
        }
        output = io.StringIO()

        ctsReport.write_text_summary(
            "candidate", results, output, include_all=True
        )

        text = output.getvalue()
        self.assertIn("all cases:", text)
        self.assertIn("geometry/triangle/passed [passed]", text)
        self.assertIn("geometry/triangle/failed [failed]", text)
        self.assertIn("geometry/triangle/skipped [skipped]", text)
        self.assertIn("missing feature", text)

    def test_text_report_includes_descriptions_for_itemized_cases(self):
        results = {
            "geometry/triangle/failed": make_sidecar(
                case="failed",
                verdict="failed",
                description="Checks triangle geometry rendering.",
            )
        }
        output = io.StringIO()

        ctsReport.write_text_summary("candidate", results, output)

        self.assertIn(
            "Description: Checks triangle geometry rendering.",
            output.getvalue(),
        )

    def test_pdf_report_includes_descriptions_for_itemized_cases(self):
        try:
            from reportlab.platypus import Paragraph as real_paragraph
        except ImportError:
            self.skipTest("reportlab is not installed")

        results = {
            "geometry/triangle/default": make_sidecar(
                description="Checks triangle geometry rendering."
            )
        }
        paragraphs = []

        def capture_paragraph(text, style, *args, **kwargs):
            paragraphs.append(text)
            return real_paragraph(text, style, *args, **kwargs)

        with tempfile.TemporaryDirectory() as temp:
            out_path = Path(temp) / "report.pdf"
            with mock.patch(
                "reportlab.platypus.Paragraph", side_effect=capture_paragraph
            ):
                self.assertTrue(
                    ctsReport.generate_pdf(
                        temp, results, out_path, include_all=True
                    )
                )

        self.assertIn(
            "Description: Checks triangle geometry rendering.", paragraphs
        )

    def test_all_flag_is_forwarded_to_pdf_generation(self):
        with tempfile.TemporaryDirectory() as temp:
            workdir = Path(temp) / "candidate"
            out_path = Path(temp) / "report.pdf"
            write_sidecar(workdir)

            with mock.patch.object(ctsReport, "generate_pdf") as generate_pdf:
                with mock.patch.object(
                    ctsReport,
                    "write_text_summary",
                    return_value={"failed": 0},
                ) as write_text_summary:
                    return_code = ctsReport.main(
                        [
                            "report",
                            str(workdir),
                            "--all",
                            "--pdf",
                            str(out_path),
                        ]
                    )

        self.assertEqual(return_code, 0)
        self.assertTrue(write_text_summary.call_args.kwargs["include_all"])
        generate_pdf.assert_called_once()
        self.assertTrue(generate_pdf.call_args.kwargs["include_all"])

    def test_default_pdf_mode_remains_failure_only(self):
        with tempfile.TemporaryDirectory() as temp:
            workdir = Path(temp) / "candidate"
            out_path = Path(temp) / "report.pdf"
            write_sidecar(workdir)

            with mock.patch.object(ctsReport, "generate_pdf") as generate_pdf:
                with mock.patch.object(
                    ctsReport,
                    "write_text_summary",
                    return_value={"failed": 0},
                ) as write_text_summary:
                    ctsReport.main(
                        ["report", str(workdir), "--pdf", str(out_path)]
                    )

        self.assertFalse(write_text_summary.call_args.kwargs["include_all"])
        self.assertFalse(generate_pdf.call_args.kwargs["include_all"])


class DeviceDiffContractTests(unittest.TestCase):
    def test_each_metric_change_is_a_semantic_difference(self):
        baseline = {"geometry/triangle/default": make_sidecar()}
        for metric in ("ssim", "psnr"):
            with self.subTest(metric=metric):
                changed = make_sidecar()
                changed["channels"][0]["metrics"][metric] -= 0.05

                diff = ctsReport.diff_results(
                    baseline, {"geometry/triangle/default": changed}
                )

                self.assertTrue(ctsReport.has_differences(diff))
                self.assertEqual(diff["cases"][0]["channels"][0]["metric"], metric)

    def test_cli_exits_zero_for_identical_sidecars(self):
        with tempfile.TemporaryDirectory() as temp:
            run_a = Path(temp) / "run-a"
            run_b = Path(temp) / "run-b"
            write_sidecar(run_a)
            write_sidecar(run_b)

            result = run_diff(run_a, run_b)

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("no differences", result.stdout)

    def test_missing_cases_and_verdict_changes_exit_one(self):
        scenarios = ("missing", "verdict")
        for scenario in scenarios:
            with self.subTest(scenario=scenario), tempfile.TemporaryDirectory() as temp:
                run_a = Path(temp) / "run-a"
                run_b = Path(temp) / "run-b"
                write_sidecar(run_a)
                verdict = "failed" if scenario == "verdict" else "passed"
                write_sidecar(run_b, verdict=verdict)
                if scenario == "missing":
                    write_sidecar(run_b, case="extra")

                result = run_diff(run_a, run_b)

            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_json_and_text_report_the_same_semantic_differences(self):
        with tempfile.TemporaryDirectory() as temp:
            run_a = Path(temp) / "run-a"
            run_b = Path(temp) / "run-b"
            write_sidecar(run_a, case="only-a")
            write_sidecar(run_b, case="only-b")
            write_sidecar(run_a, case="verdict")
            write_sidecar(run_b, case="verdict", verdict="failed")
            write_sidecar(run_a, case="metric", ssim=0.95)
            write_sidecar(run_b, case="metric", ssim=0.90)

            text_result = run_diff(run_a, run_b)
            json_result = run_diff(run_a, run_b, "--json")

        payload = json.loads(json_result.stdout)
        expected_keys = {
            *payload["only_in_a"],
            *payload["only_in_b"],
            *(entry["case"] for entry in payload["verdict_changed"]),
            *(
                entry["case"]
                for entry in payload["cases"]
                if entry["channels"]
            ),
        }
        self.assertEqual(text_result.returncode, 1)
        self.assertEqual(json_result.returncode, 1)
        self.assertEqual(
            expected_keys,
            {
                "geometry/triangle/only-a",
                "geometry/triangle/only-b",
                "geometry/triangle/verdict",
                "geometry/triangle/metric",
            },
        )
        for key in expected_keys:
            self.assertIn(key, text_result.stdout)

    def test_timing_only_change_is_not_a_semantic_difference(self):
        with tempfile.TemporaryDirectory() as temp:
            run_a = Path(temp) / "run-a"
            run_b = Path(temp) / "run-b"
            write_sidecar(run_a, duration_ms=10.0)
            write_sidecar(run_b, duration_ms=25.0)

            diff = ctsReport.diff_results(
                ctsReport.load_results(run_a), ctsReport.load_results(run_b)
            )
            result = run_diff(run_a, run_b)

        self.assertFalse(ctsReport.has_differences(diff))
        self.assertEqual(diff["cases"], [])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("no differences", result.stdout)

    def test_cli_exits_one_for_metric_only_change(self):
        with tempfile.TemporaryDirectory() as temp:
            run_a = Path(temp) / "run-a"
            run_b = Path(temp) / "run-b"
            write_sidecar(run_a, ssim=0.95)
            write_sidecar(run_b, ssim=0.90)

            result = run_diff(run_a, run_b)

        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("geometry/triangle/default [color/ssim]", result.stdout)


if __name__ == "__main__":
    unittest.main()
