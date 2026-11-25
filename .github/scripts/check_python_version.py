#!/usr/bin/env python3
# Copyright 2026 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

"""Check that pyproject.toml's version is ANARI_SDK_VERSION, or a post-release
of it (0.17.0.post1), and for a release tag, that the tag names that version
and PyPI doesn't have it yet: it never accepts one twice.

A post-release's tag spells .postN as -N (v0.17.0-1 for 0.17.0.post1), the
alternative spelling PEP 440 allows; other releases are tagged v<version>."""

import argparse
import pathlib
import re
import sys
import tomllib
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]


def sdk_version():
    cmake = (ROOT / 'CMakeLists.txt').read_text()
    return '.'.join(
        re.search(r'set\(ANARI_SDK_VERSION_%s (\d+)\)' % part, cmake).group(1)
        for part in ('MAJOR', 'MINOR', 'PATCH'))


def package_version():
    with open(ROOT / 'pyproject.toml', 'rb') as f:
        return tomllib.load(f)['project']['version']


def release_tag(version):
    """The tag that releases this version: v0.17.0, or v0.17.0-1 for
    0.17.0.post1."""
    return 'v' + re.sub(r'\.post(\d+)$', r'-\1', version)


def on_pypi(version):
    """True if PyPI has this release of anari, yanked or not."""
    url = 'https://pypi.org/pypi/anari/%s/json' % version
    try:
        with urllib.request.urlopen(url, timeout=30):
            return True
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return False
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', help='the release tag being built (vX.Y.Z)')
    parser.add_argument('--github-output', type=pathlib.Path,
                        help='append the version here ($GITHUB_OUTPUT)')
    args = parser.parse_args()

    sdk, version = sdk_version(), package_version()
    print('anari %s (SDK %s)' % (version, sdk))
    errors = []
    if not re.fullmatch(re.escape(sdk) + r'(\.post\d+)?', version):
        errors.append('the version is %s: it must be the SDK version %s, or a '
                      'post-release of it (%s.post1, ...)' % (version, sdk, sdk))
    if args.tag is not None:
        if args.tag != release_tag(version):
            errors.append('the tag %s is not %s, the tag for version %s'
                          % (args.tag, release_tag(version), version))
        elif on_pypi(version):
            errors.append('PyPI already has anari %s, and never accepts a '
                          'version twice: release %s.postN (tagged v%s-N) '
                          'for a packaging-only respin' % (version, sdk, sdk))
    for e in errors:
        print('::error::' + e)
    if errors:
        return 1

    if args.github_output:
        with open(args.github_output, 'a') as f:
            f.write('version=%s\n' % version)
    return 0


if __name__ == '__main__':
    sys.exit(main())
