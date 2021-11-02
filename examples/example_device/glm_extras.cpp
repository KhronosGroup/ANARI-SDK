// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "glm_extras.h"
// std
#include <random>
// openmp
#if _OPENMP
#include <omp.h>
#endif

namespace anari {
namespace example_device {

using RNG_T = std::ranlux24_base;

std::vector<RNG_T> make_rngs()
{
  std::vector<RNG_T> v;
  v.resize(256);
  return v;
}

static std::vector<RNG_T> g_rng = make_rngs();
static std::uniform_real_distribution<float> g_dist_0_1{0.f, 1.f};
static std::uniform_real_distribution<float> g_dist_neg_1_1{-1.f, 1.f};

static RNG_T &getRNG()
{
#if _OPENMP
  const int idx = omp_get_thread_num();
#else
  const int idx = 1;
#endif

  return g_rng[idx];
}

float randUniformDist()
{
  return g_dist_0_1(getRNG());
}

vec3 randomDir()
{
  auto &rng = getRNG();
  vec3 dir(g_dist_neg_1_1(rng), g_dist_neg_1_1(rng), g_dist_neg_1_1(rng));
  return normalize(dir);
}

} // namespace example_device
} // namespace anari