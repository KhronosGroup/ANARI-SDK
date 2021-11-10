#!/bin/bash -x

## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

set -e

### Install dependencies ###

apt update
apt -y install g++ wget

### Run tests ###

export LD_LIBRARY_PATH=`pwd`/install/lib:$LD_LIBRARY_PATH
export PATH=`pwd`/install/bin:$PATH

mkdir images
cd images
anari_regression_tests --library $1
