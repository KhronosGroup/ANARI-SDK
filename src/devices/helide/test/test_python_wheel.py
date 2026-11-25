# Copyright 2026 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

"""Smoke-test the installed helide wheel."""


def test_installed_wheel_loads_device():
    import helide

    assert helide.library is not None
    device = helide.newDevice()
    assert device is not None
    device.commitParameters()
