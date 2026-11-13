# Copyright 2026 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

"""python -m anari --cmake-dir: the anari_DIR for building a device against
the ANARI SDK in this package.

  cmake -Danari_DIR="$(python -m anari --cmake-dir)" ...
"""

import argparse
import sys

import anari


def main(argv=None):
    parser = argparse.ArgumentParser(prog='python -m anari')
    parser.add_argument('--cmake-dir', action='store_true', required=True,
                        help='print the directory holding anariConfig.cmake')
    parser.parse_args(argv)
    path = anari.cmake_dir()
    if path is None:
        print('anari: no SDK found in ' + str(anari.__file__), file=sys.stderr)
        return 1
    print(path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
