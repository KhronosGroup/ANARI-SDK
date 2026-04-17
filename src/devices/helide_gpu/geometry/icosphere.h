// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace helide_gpu {

// Icosahedron with 12 vertices and 20 triangles (60 indices).
// Vertex positions lie on the unit sphere.
// clang-format off

static constexpr uint32_t ico_vertex_count = 12;
static constexpr float ico_vertices[][3] = {
  { 0.000000f,  1.000000f,  0.000000f},
  { 0.000000f, -1.000000f,  0.000000f},
  { 0.723600f,  0.447215f,  0.525720f},
  {-0.276385f,  0.447215f,  0.850640f},
  {-0.894425f,  0.447215f,  0.000000f},
  {-0.276385f,  0.447215f, -0.850640f},
  { 0.723600f,  0.447215f, -0.525720f},
  { 0.276385f, -0.447215f,  0.850640f},
  {-0.723600f, -0.447215f,  0.525720f},
  {-0.723600f, -0.447215f, -0.525720f},
  { 0.276385f, -0.447215f, -0.850640f},
  { 0.894425f, -0.447215f,  0.000000f},
};

static constexpr uint32_t ico_index_count = 60;
static constexpr uint32_t ico_indices[] = {
   0,  2,  3,
   0,  3,  4,
   0,  4,  5,
   0,  5,  6,
   0,  6,  2,
   1,  7,  8,
   1,  8,  9,
   1,  9, 10,
   1, 10, 11,
   1, 11,  7,
   2,  7,  3,
   3,  8,  4,
   4,  9,  5,
   5, 10,  6,
   6, 11,  2,
   7,  2, 11,
   8,  3,  7,
   9,  4,  8,
  10,  5,  9,
  11,  6, 10,
};

// The circumradius factor: an icosahedron inscribed in a unit sphere has
// vertices at distance 1 from the centre, but the midpoints of its faces are
// at distance ~0.7558.  Scaling vertices by this factor guarantees every face
// lies outside (or on) the unit sphere, so the rasterised hull fully encloses
// any unit sphere.
static constexpr float ico_circumradius_factor = 1.26f;

// clang-format on

} // namespace helide_gpu
