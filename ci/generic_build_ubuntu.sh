#!/bin/bash -x

## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

set -e

### Install dependencies ###

export DEBIAN_FRONTEND=noninteractive
apt update
apt -y install cmake g++ git make wget python3

### Build ###

mkdir build
cd build

cmake --version

cmake -DCMAKE_INSTALL_PREFIX=../install $@ ..

make -j `nproc` install

### Run tests ###

make test
