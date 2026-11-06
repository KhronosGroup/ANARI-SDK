#!/usr/bin/env python3
# Copyright 2026 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--implib-gen", required=True)
    parser.add_argument("library")
    parser.add_argument("--soname", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--stub-basename", required=True)
    args = parser.parse_args()

    library = Path(args.library)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    if not args.soname:
        parser.error(
            "could not determine SDL3's SONAME; set -DSDL3_SONAME=<soname>"
        )

    with tempfile.TemporaryDirectory(prefix="implib-", dir=output_dir) as temp:
        subprocess.run(
            [
                sys.executable,
                args.implib_gen,
                "-q",
                "--library-load-name",
                args.soname,
                "--outdir",
                temp,
                str(library),
            ],
            check=True,
        )

        for suffix in (".tramp.S", ".init.c"):
            shutil.copyfile(
                Path(temp) / f"{library.name}{suffix}",
                output_dir / f"{args.stub_basename}{suffix}",
            )

    symbol_suffix = re.sub(r"[^a-zA-Z_0-9]+", "_", library.name)
    soname = args.soname.replace("\\", "\\\\").replace('"', '\\"')
    (output_dir / "sdl3_runtime_config.h").write_text(
        "#pragma once\n"
        f'#define HELIDE_GPU_SDL3_SONAME "{soname}"\n'
    )
    (output_dir / "sdl3_trampoline_symbol.h").write_text(
        "#pragma once\n"
        f"#define HELIDE_GPU_SDL3_SET_HANDLE _{symbol_suffix}_tramp_set_handle\n"
    )


if __name__ == "__main__":
    main()
