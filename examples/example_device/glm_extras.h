// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <glm/gtx/component_wise.hpp>
// std
#include <limits>
#include <optional>
#include <random>
#include <variant>
// anari
#include "anari/anari_cpp/ext/glm.h"

namespace anari {
namespace example_device {

struct Material;
struct Volume;

using namespace glm;

float randUniformDist();
vec3 randomDir();

// AABB type //////////////////////////////////////////////////////////////////

template <typename T>
struct range_t
{
  using element_t = T;

  range_t() = default;
  range_t(const T &t) : lower(t), upper(t) {}
  range_t(const T &_lower, const T &_upper) : lower(_lower), upper(_upper) {}

  range_t<T> &extend(const T &t)
  {
    lower = min(lower, t);
    upper = max(upper, t);
    return *this;
  }

  range_t<T> &extend(const range_t<T> &t)
  {
    lower = min(lower, t.lower);
    upper = max(upper, t.upper);
    return *this;
  }

  T lower{T(std::numeric_limits<float>::max())};
  T upper{T(-std::numeric_limits<float>::max())};
};

using box1 = range_t<float>;
using box3 = range_t<vec3>;

// Operations on range_t //

template <typename T>
inline typename range_t<T>::element_t size(const range_t<T> &r)
{
  return r.upper - r.lower;
}

template <typename T>
inline typename range_t<T>::element_t center(const range_t<T> &r)
{
  return .5f * (r.lower + r.upper);
}

template <typename T>
inline typename range_t<T>::element_t clamp(
    const typename range_t<T>::element_t &t, const range_t<T> &r)
{
  return max(r.lower, min(t, r.upper));
}

template <typename T>
inline size_t largest_axis(const range_t<T> &r)
{
  return 0;
}

template <>
inline size_t largest_axis(const box3 &r)
{
  auto d = size(r);
  size_t axis = 0;
  if (d[0] < d[1])
    axis = 1;
  if (d[axis] < d[2])
    axis = 2;
  return axis;
}

inline float largest_extent(const box3 &r)
{
  return size(r)[largest_axis(r)];
}

inline float half_area(const box3 &r)
{
  auto d = size(r);
  return (d[0] + d[1]) * d[2] + d[0] * d[1];
}

inline float volume(const box3 &r)
{
  auto d = size(r);
  return d[0] * d[1] * d[2];
}

inline bool empty(const box3 &r)
{
  return r.lower.x > r.upper.x || r.lower.y > r.upper.y
      || r.lower.z > r.upper.z;
}

// Ray type ///////////////////////////////////////////////////////////////////

struct Ray
{
  vec3 org;
  vec3 dir;
  box1 t{0.f, std::numeric_limits<float>::infinity()};
  bool skipVolumes{false}; // hack for surfaces found inside volumes
  mutable const void *currentSurface{nullptr};
};

struct GeometryInfo
{
  GeometryInfo() = default;
  GeometryInfo(vec3 normal_) : normal(normal_) {}

  float u{0.f}, v{0.f};
  vec3 normal;

  const Material *material{nullptr};
  vec4 color{1};
};

struct VolumeInfo
{
  VolumeInfo() = default;

  const Volume *model{nullptr};
};

struct Hit
{
  box1 t;

  std::optional<uint32_t> primID;
  std::optional<uint32_t> geomID;
  std::optional<uint32_t> volID;
  std::optional<uint32_t> instID;

  std::variant<GeometryInfo, VolumeInfo> info;
};

using PotentialHit = std::optional<Hit>;

// Helpers //

template <typename INFO_T>
inline Hit makeHit(box1 t, uint32_t primID, INFO_T &info)
{
  Hit result;

  result.t = t;
  result.primID = primID;
  result.info = info;

  return result;
}

template <typename INFO_T>
inline Hit makeHit(box1 t, INFO_T &info)
{
  Hit result;

  result.t = t;
  result.info = info;

  return result;
}

inline bool hitGeometry(const PotentialHit &h)
{
  return h.has_value() && std::holds_alternative<GeometryInfo>(h->info);
}

inline bool hitVolume(const PotentialHit &h)
{
  return h.has_value() && std::holds_alternative<VolumeInfo>(h->info);
}

inline GeometryInfo &getGeometryInfo(PotentialHit &h)
{
  return std::get<GeometryInfo>(h->info);
}

inline const GeometryInfo &getGeometryInfo(const PotentialHit &h)
{
  return std::get<GeometryInfo>(h->info);
}

inline VolumeInfo &getVolumeInfo(PotentialHit &h)
{
  return std::get<VolumeInfo>(h->info);
}

inline const VolumeInfo &getVolumeInfo(const PotentialHit &h)
{
  return std::get<VolumeInfo>(h->info);
}

// Helper functions ///////////////////////////////////////////////////////////

inline size_t longProduct(uvec3 v)
{
  return size_t(v.x) * size_t(v.y) * size_t(v.z);
}

inline vec3 xfmVec(const mat4 &m, const vec3 &p)
{
  return vec3(mat3(m) * vec(p));
}

inline vec3 xfmPoint(const mat4 &m, const vec3 &p)
{
  return vec3(m * vec4(p, 1.f));
}

inline Ray xfmRay(const mat4 &m, const Ray &r)
{
  Ray retval = r;
  retval.org = xfmPoint(m, r.org);
  retval.dir = xfmVec(m, r.dir);
  return retval;
}

inline box3 xfmBox(const mat4 &m, const box3 &b)
{
  box3 retval;
  retval.extend(xfmPoint(m, vec3(b.lower.x, b.lower.y, b.lower.z)));
  retval.extend(xfmPoint(m, vec3(b.lower.x, b.lower.y, b.upper.z)));
  retval.extend(xfmPoint(m, vec3(b.lower.x, b.upper.y, b.lower.z)));
  retval.extend(xfmPoint(m, vec3(b.lower.x, b.upper.y, b.upper.z)));
  retval.extend(xfmPoint(m, vec3(b.upper.x, b.lower.y, b.lower.z)));
  retval.extend(xfmPoint(m, vec3(b.upper.x, b.lower.y, b.upper.z)));
  retval.extend(xfmPoint(m, vec3(b.upper.x, b.upper.y, b.lower.z)));
  retval.extend(xfmPoint(m, vec3(b.upper.x, b.upper.y, b.upper.z)));
  return retval;
}

inline vec3 makeRandomColor(uint32_t i)
{
  const uint32_t mx = 13 * 17 * 43;
  const uint32_t my = 11 * 29;
  const uint32_t mz = 7 * 23 * 63;
  const uint32_t g = (i * (3 * 5 * 127) + 12312314);
  return vec3((g % mx) * (1.f / (mx - 1)),
      (g % my) * (1.f / (my - 1)),
      (g % mz) * (1.f / (mz - 1)));
}

} // namespace example_device

ANARI_TYPEFOR_SPECIALIZATION(example_device::box1, ANARI_FLOAT32_BOX1);
ANARI_TYPEFOR_SPECIALIZATION(example_device::box3, ANARI_FLOAT32_BOX3);

} // namespace anari
