# Copyright 2026 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

"""Render a simple triangle through the installed helide_gpu wheel."""

from array import array
import os
import sys


def _check(condition, reason, messages):
    if not condition:
        details = "\n".join(messages) or "<no ANARI status messages>"
        raise AssertionError(f"{reason}\nANARI status messages:\n{details}")


def _require_gpu(messages):
    import pytest

    gpu_status = next(
        (message for message in messages if "[SDL3_gpu] Device created (" in message),
        None,
    )
    if gpu_status is None:
        backend_failure = next(
            (
                message
                for message in messages
                if "[SDL3_gpu] SDL_CreateGPUDevice failed:" in message
            ),
            None,
        )
        if backend_failure is not None:
            details = "\n".join(messages)
            pytest.skip(f"no SDL GPU backend available\n{details}")
    _check(gpu_status is not None, "SDL GPU device status was not reported", messages)
    return gpu_status


def test_installed_wheel_renders_triangle():
    if sys.platform.startswith("linux"):
        os.environ.setdefault("SDL_VIDEODRIVER", "offscreen")

    import anari
    import helide_gpu

    messages = []
    helide_gpu.set_status_callback(
        lambda device, source, source_type, severity, code, message: messages.append(
            message
        )
    )
    library = helide_gpu.library
    _check(library is not None, "could not load helide_gpu library", messages)

    device = library.newDevice("default")
    _check(device is not None, "could not create helide_gpu device", messages)
    device.commitParameters()

    camera = device.newCamera("perspective")
    gpu_status = _require_gpu(messages)
    camera.setParameter("position", anari.FLOAT32_VEC3, [0.0, 0.0, 3.0])
    camera.setParameter("direction", anari.FLOAT32_VEC3, [0.0, 0.0, -1.0])
    camera.setParameter("up", anari.FLOAT32_VEC3, [0.0, 1.0, 0.0])
    camera.setParameter("fovy", anari.FLOAT32, 1.04719755)
    camera.setParameter("aspect", anari.FLOAT32, 1.0)
    camera.commitParameters()

    geometry = device.newGeometry("triangle")
    geometry.setParameterArray1D(
        "vertex.position",
        anari.FLOAT32_VEC3,
        array("f", [-1.5, -1.5, 0.0, 1.5, -1.5, 0.0, 0.0, 1.5, 0.0]),
    )
    geometry.setParameterArray1D(
        "vertex.normal", anari.FLOAT32_VEC3, array("f", [0.0, 0.0, 1.0] * 3)
    )
    geometry.commitParameters()

    material = device.newMaterial("matte")
    material.commitParameters()
    surface = device.newSurface()
    surface.setParameter("geometry", anari.GEOMETRY, geometry)
    surface.setParameter("material", anari.MATERIAL, material)
    surface.commitParameters()

    world = device.newWorld()
    world.setParameterArray1D("surface", anari.SURFACE, [surface])
    world.commitParameters()

    renderer = device.newRenderer("default")
    renderer.setParameter(
        "background", anari.FLOAT32_VEC4, [0.0, 0.0, 0.0, 1.0]
    )
    renderer.commitParameters()

    frame = device.newFrame()
    frame.setParameter("size", anari.UINT32_VEC2, [64, 64])
    frame.setParameter("channel.color", anari.DATA_TYPE, anari.UFIXED8_VEC4)
    frame.setParameter("camera", anari.CAMERA, camera)
    frame.setParameter("world", anari.WORLD, world)
    frame.setParameter("renderer", anari.RENDERER, renderer)
    frame.commitParameters()
    frame.renderFrame()
    _check(frame.frameReady(anari.WAIT), "helide_gpu frame did not complete", messages)

    _check(
        "backend: " in gpu_status and "backend: )" not in gpu_status,
        f"SDL GPU backend was not reported: {gpu_status}",
        messages,
    )
    if "adapter: " in gpu_status:
        adapter = gpu_status.split("adapter: ", 1)[1].split(",", 1)[0]
        _check(adapter, f"SDL GPU adapter was reported empty: {gpu_status}", messages)
    if "driver: " in gpu_status:
        driver = gpu_status.split("driver: ", 1)[1].split(")", 1)[0]
        _check(driver, f"SDL GPU driver was reported empty: {gpu_status}", messages)

    pixels, width, height, _ = frame.mapFrame("channel.color")
    _check(pixels != anari.ffi.NULL, "helide_gpu color channel did not map", messages)
    center_offset = ((height // 2) * width + width // 2) * 4
    center = bytes(anari.ffi.buffer(pixels + center_offset, 4))
    frame.unmapFrame("channel.color")
    _check(
        max(center[:3]) > 10,
        "triangle did not render at the image center",
        messages,
    )
