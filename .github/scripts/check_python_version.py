#!/usr/bin/env python3
# Copyright 2026 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

"""Check that pyproject.toml's version is ANARI_SDK_VERSION, or a post-release
of it (0.17.0.post1)."""

import pathlib
import re
import sys
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[2]


def sdk_version():
    cmake = (ROOT / 'CMakeLists.txt').read_text()
    return '.'.join(
        re.search(r'set\(ANARI_SDK_VERSION_%s (\d+)\)' % part, cmake).group(1)
        for part in ('MAJOR', 'MINOR', 'PATCH'))


def package_version():
    with open(ROOT / 'pyproject.toml', 'rb') as f:
        return tomllib.load(f)['project']['version']


def main():
    sdk, version = sdk_version(), package_version()
    print('anari %s (SDK %s)' % (version, sdk))
    if not re.fullmatch(re.escape(sdk) + r'(\.post\d+)?', version):
        print('::error::the version is %s: it must be the SDK version %s, or a '
              'post-release of it (%s.post1, ...)' % (version, sdk, sdk))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
