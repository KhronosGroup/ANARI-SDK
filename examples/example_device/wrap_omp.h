// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef _OPENMP
#include <omp.h>
#include <cassert>
#define OMP_GET_NUM_THREADS() omp_get_num_threads()
#define OMP_GET_THREAD_NUM() omp_get_thread_num()
#define ASSERT_SERIAL() assert(omp_get_level() == 0);
#define ASSERT_PARALLEL() assert(omp_get_level() > 0);
#else
#define OMP_GET_NUM_THREADS() 1
#define OMP_GET_THREAD_NUM() 0
#define ASSERT_SERIAL() (void)0
#define ASSERT_PARALLEL() (void)0
#endif
