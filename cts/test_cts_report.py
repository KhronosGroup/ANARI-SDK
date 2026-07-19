#!/usr/bin/env python3
# Copyright 2026 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

# ctsReport.py is retained only for the PDF (ADR-0008); text summaries and the
# device diff moved to the C++ `anariCts report`/`diff` and are covered by the
# C++ tests (test_cts_report.cpp). These tests cover the shared sidecar-tree
# reading the PDF path still depends on, plus PDF generation itself.

import base64
import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from cts import ctsReport


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


class SidecarLoadingTests(unittest.TestCase):
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

    def test_summarize_counts_rendered_skipped_and_behavioral_cases(self):
        failed = make_sidecar(case="failed", verdict="failed")
        skipped = make_sidecar(case="skipped", verdict="skipped")
        skipped["channels"] = []
        results = {
            "geometry/triangle/passed": make_sidecar(case="passed"),
            "geometry/triangle/failed": failed,
            "geometry/triangle/skipped": skipped,
        }

        summary = ctsReport.summarize(results)

        self.assertEqual(summary["total"], 3)
        self.assertEqual(summary["passed"], 1)
        self.assertEqual(summary["failed"], 1)
        self.assertEqual(summary["skipped"], 1)
        self.assertEqual(summary["failures"], ["geometry/triangle/failed"])


class PdfReportTests(unittest.TestCase):
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

    def test_pdf_image_comparison_tables_fit_page(self):
        try:
            from reportlab.lib.pagesizes import A4
            from reportlab.platypus import Image as ReportImage
            from reportlab.platypus import SimpleDocTemplate
            from reportlab.platypus import Table as real_table
        except ImportError:
            self.skipTest("reportlab is not installed")

        failed = make_sidecar(verdict="failed")
        failed["channels"][0].update(
            {
                "resultImage": "image.png",
                "groundTruthImage": "image.png",
                "diffImage": "image.png",
                "thresholdImage": "image.png",
            }
        )
        image_tables = []

        def capture_table(rows, *args, **kwargs):
            table = real_table(rows, *args, **kwargs)
            if any(
                isinstance(cell, ReportImage)
                for row in rows
                for cell in row
            ):
                image_tables.append(table)
            return table

        with tempfile.TemporaryDirectory() as temp:
            workdir = Path(temp)
            out_path = workdir / "report.pdf"
            image_bytes = base64.b64decode(
                "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwC"
                "AAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII="
            )
            (workdir / "image.png").write_bytes(image_bytes)

            with mock.patch(
                "reportlab.platypus.Table", side_effect=capture_table
            ):
                self.assertTrue(
                    ctsReport.generate_pdf(
                        workdir,
                        {"geometry/triangle/default": failed},
                        out_path,
                    )
                )

            content_width = SimpleDocTemplate(
                str(out_path), pagesize=A4
            ).width

        self.assertEqual(len(image_tables), 1)
        table_width, _ = image_tables[0].wrap(content_width, A4[1])
        self.assertLessEqual(table_width, content_width)

    def test_all_flag_is_forwarded_to_pdf_generation(self):
        with tempfile.TemporaryDirectory() as temp:
            workdir = Path(temp) / "candidate"
            out_path = Path(temp) / "report.pdf"
            write_sidecar(workdir)

            with mock.patch.object(ctsReport, "generate_pdf") as generate_pdf:
                return_code = ctsReport.main(
                    ["report", str(workdir), "--all", "--pdf", str(out_path)]
                )

        self.assertEqual(return_code, 0)
        generate_pdf.assert_called_once()
        self.assertTrue(generate_pdf.call_args.kwargs["include_all"])

    def test_default_pdf_mode_remains_failure_only(self):
        with tempfile.TemporaryDirectory() as temp:
            workdir = Path(temp) / "candidate"
            out_path = Path(temp) / "report.pdf"
            write_sidecar(workdir)

            with mock.patch.object(ctsReport, "generate_pdf") as generate_pdf:
                ctsReport.main(["report", str(workdir), "--pdf", str(out_path)])

        self.assertFalse(generate_pdf.call_args.kwargs["include_all"])

    def test_pdf_output_path_is_required(self):
        with self.assertRaises(SystemExit):
            ctsReport.main(["report", "somewhere"])


if __name__ == "__main__":
    unittest.main()
