# Copyright 2026 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

"""Check Python runtime loading without requiring SDL3 or a GPU."""

import ctypes
import importlib.util
import os
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch


RTLD_NOW = 2
RTLD_GLOBAL = 8


class TestPythonTrampoline(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = Path(__file__).parent.parent / "cmake/python_trampoline.py.in"
        if not source.is_file():
            # Wheel validation checks out only test/, not the device sources.
            source = Path(importlib.util.find_spec("helide_gpu").origin)
        cls.source = source
        cls.code = compile(
            source.read_text().replace("${DEVICE_LIBRARY_NAME}", "helide_gpu"),
            str(source),
            "exec",
        )

    def run_trampoline(self, platform, override=None, pysdl=True, error=None):
        calls = []
        runtime = object()
        sdl_path = str(self.source.parent / "libSDL3.dylib")

        def load_sdl(path, mode):
            calls.append(("sdl", path, mode))
            if error is not None:
                raise error
            return runtime

        def load_device(name, status):
            calls.append(("device",))

        anari = SimpleNamespace(
            error_status_handle=object(),
            ffi=SimpleNamespace(
                from_handle=lambda handle: handle,
                new_handle=lambda handle: handle,
            ),
            loadLibrary=load_device,
        )
        sdl3 = SimpleNamespace(
            __file__=str(self.source.parent / "sdl3/__init__.py"),
            SDL_GET_BINARY=lambda name: SimpleNamespace(_name=sdl_path),
        )
        namespace = {"__file__": str(self.source)}
        with (
            patch.dict(sys.modules, anari=anari, sdl3=sdl3 if pysdl else None),
            patch.dict(os.environ),
            patch.object(sys, "platform", platform),
            patch.object(os, "RTLD_NOW", RTLD_NOW, create=True),
            patch.object(os, "RTLD_GLOBAL", RTLD_GLOBAL, create=True),
            patch.object(Path, "exists", return_value=True),
            patch.object(ctypes, "CDLL", side_effect=load_sdl),
        ):
            os.environ.pop("HELIDE_GPU_SDL3_LIBRARY", None)
            if override is not None:
                os.environ["HELIDE_GPU_SDL3_LIBRARY"] = override
            exec(self.code, namespace)
        return calls, namespace, runtime, sdl_path

    def test_macos_preloads_pysdl_runtime_before_device(self):
        calls, namespace, runtime, path = self.run_trampoline("darwin")
        path = str(Path(path).resolve())
        self.assertEqual(
            calls,
            [("sdl", path, RTLD_NOW | RTLD_GLOBAL), ("device",)],
        )
        self.assertIs(namespace["_sdl_runtime"], runtime)

    def test_macos_honors_runtime_override(self):
        for pysdl in (True, False):
            with self.subTest(pysdl=pysdl):
                calls, _, _, _ = self.run_trampoline(
                    "darwin", override="/custom/SDL3", pysdl=pysdl
                )
                self.assertEqual(
                    calls,
                    [
                        ("sdl", "/custom/SDL3", RTLD_NOW | RTLD_GLOBAL),
                        ("device",),
                    ],
                )

    def test_macos_without_pysdl_uses_default_library_name(self):
        calls, _, _, _ = self.run_trampoline("darwin", pysdl=False)
        self.assertEqual(
            calls,
            [("sdl", "libSDL3.dylib", RTLD_NOW | RTLD_GLOBAL), ("device",)],
        )

    def test_other_platforms_keep_deferred_loading(self):
        for platform in ("linux", "win32"):
            with self.subTest(platform=platform):
                calls, namespace, _, _ = self.run_trampoline(platform)
                self.assertEqual(calls, [("device",)])
                self.assertNotIn("_sdl_runtime", namespace)

    def test_macos_reports_preload_failure(self):
        error = OSError("SDL3 could not be loaded")
        with self.assertRaises(OSError) as caught:
            self.run_trampoline("darwin", error=error)
        self.assertIs(caught.exception, error)


if __name__ == "__main__":
    unittest.main()
