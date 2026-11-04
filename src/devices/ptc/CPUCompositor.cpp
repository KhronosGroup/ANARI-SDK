// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// ptc
#include "CPUCompositor.h"
// tbb
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>
// std
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

namespace anari_ptc {

namespace {

int divRoundUp(int a, int b)
{
  return (a + b - 1) / b;
}

/*! depth that stands in for 'nothing hit' while sorting; any input depth
    at or beyond it (+inf, or FLT_MAX as helide writes), and NaN, is a
    miss */
constexpr float NO_HIT_DEPTH = 1e20f;

/*! one rank's fragment of a pixel, for sorting: nearest first, and on
    equal depth by rank, so the order does not depend on the sort */
struct Fragment
{
  float depth;
  uint32_t rank;

  bool operator<(const Fragment &other) const;
};

bool Fragment::operator<(const Fragment &other) const
{
  return depth < other.depth || (depth == other.depth && rank < other.rank);
}

/*! [0,1] to 0..255, rounded; out-of-range values (and NaN) clamp */
inline uint32_t toUnorm8(float v)
{
  v = v * 255.f + .5f;
  if (!(v > 0.f))
    return 0;
  if (v >= 255.f)
    return 255;
  return uint32_t(v);
}

/*! ColorFormat::RGBA8: one uint32_t per pixel, r in the low byte */
struct RGBA8Pixel
{
  using Element = uint32_t;
  static math::float4 load(const Element *buf, size_t i);
  static void store(Element *buf, size_t i, const math::float4 &c);
};

math::float4 RGBA8Pixel::load(const Element *buf, size_t i)
{
  const uint32_t v = buf[i];
  return math::float4(float((v >> 0) & 0xff),
             float((v >> 8) & 0xff),
             float((v >> 16) & 0xff),
             float((v >> 24) & 0xff))
      * (1.f / 255.f);
}

void RGBA8Pixel::store(Element *buf, size_t i, const math::float4 &c)
{
  buf[i] = toUnorm8(c.x) | (toUnorm8(c.y) << 8) | (toUnorm8(c.z) << 16)
      | (toUnorm8(c.w) << 24);
}

/*! IEC 61966-2-1 sRGB transfer functions, as helide's (and GPU sRGB
    formats') */
inline float srgbToLinear(float v)
{
  return v <= 0.04045f ? v * (1.f / 12.92f)
                       : std::pow((v + 0.055f) * (1.f / 1.055f), 2.4f);
}
inline float linearToSrgb(float v)
{
  return v <= 0.0031308f ? 12.92f * v
                         : 1.055f * std::pow(v, 1.f / 2.4f) - 0.055f;
}

/*! ColorFormat::RGBA8_SRGB: as RGBA8, but rgb are sRGB-encoded (from
    premultiplied linear rgb, per channel); load() decodes to linear and
    store() encodes again. Alpha is linear. */
struct RGBA8SRGBPixel
{
  using Element = uint32_t;
  static math::float4 load(const Element *buf, size_t i);
  static void store(Element *buf, size_t i, const math::float4 &c);
};

math::float4 RGBA8SRGBPixel::load(const Element *buf, size_t i)
{
  static const auto toLinear = [] {
    std::array<float, 256> lut;
    for (int code = 0; code < 256; code++)
      lut[code] = srgbToLinear(code * (1.f / 255.f));
    return lut;
  }();
  const uint32_t v = buf[i];
  return math::float4(toLinear[(v >> 0) & 0xff],
      toLinear[(v >> 8) & 0xff],
      toLinear[(v >> 16) & 0xff],
      ((v >> 24) & 0xff) * (1.f / 255.f));
}

void RGBA8SRGBPixel::store(Element *buf, size_t i, const math::float4 &c)
{
  RGBA8Pixel::store(buf,
      i,
      math::float4(
          linearToSrgb(c.x), linearToSrgb(c.y), linearToSrgb(c.z), c.w));
}

/*! ColorFormat::RGBA32F: four floats per pixel, r first */
struct RGBA32FPixel
{
  using Element = float;
  static math::float4 load(const Element *buf, size_t i);
  static void store(Element *buf, size_t i, const math::float4 &c);
};

math::float4 RGBA32FPixel::load(const Element *buf, size_t i)
{
  const Element *p = buf + 4 * i;
  return math::float4(p[0], p[1], p[2], p[3]);
}

void RGBA32FPixel::store(Element *buf, size_t i, const math::float4 &c)
{
  Element *p = buf + 4 * i;
  p[0] = c.x;
  p[1] = c.y;
  p[2] = c.z;
  p[3] = c.w;
}

/*! 32-bit elements per pixel of color: one uint32_t or four floats */
int elementsPerPixel(CPUCompositor::ColorFormat format)
{
  return format == CPUCompositor::ColorFormat::RGBA32F ? 4 : 1;
}

MPI_Datatype elementType(CPUCompositor::ColorFormat format)
{
  return format == CPUCompositor::ColorFormat::RGBA32F ? MPI_FLOAT
                                                       : MPI_UINT32_T;
}

} // namespace

OwnedDatatype::OwnedDatatype(int count, MPI_Datatype element)
{
  MPI_Type_contiguous(count, element, &m_type);
  MPI_Type_commit(&m_type);
}

void OwnedDatatype::reset()
{
  if (m_type == MPI_DATATYPE_NULL)
    return;
  int finalized = 0;
  MPI_Finalized(&finalized);
  if (!finalized)
    MPI_Type_free(&m_type);
  m_type = MPI_DATATYPE_NULL;
}

OwnedComm OwnedComm::dup(MPI_Comm comm)
{
  OwnedComm c;
  if (comm != MPI_COMM_NULL)
    MPI_Comm_dup(comm, &c.m_comm);
  return c;
}

void OwnedComm::reset()
{
  if (m_comm == MPI_COMM_NULL)
    return;
  int finalized = 0;
  MPI_Finalized(&finalized);
  if (!finalized)
    MPI_Comm_free(&m_comm);
  m_comm = MPI_COMM_NULL;
}

CPUCompositor::CPUCompositor(OwnedComm &comm)
{
  MPI_Comm_rank(comm.get(), &m_mpi.rank);
  MPI_Comm_size(comm.get(), &m_mpi.size);
  m_p2p.sendOffset.resize(m_mpi.size);
  m_p2p.sendCount.resize(m_mpi.size);
  m_p2p.recvOffset.resize(m_mpi.size);
  m_p2p.recvCount.resize(m_mpi.size);

  if (m_mpi.rank == 0) {
    m_finalGather.rank0.recvCount.resize(m_mpi.size);
    m_finalGather.rank0.recvOffset.resize(m_mpi.size);
  }
  // last, so that a throw above leaves 'comm' with the caller
  m_mpi.comm = std::move(comm);
}

CPUCompositor::~CPUCompositor()
{
  // best effort: returns false without waiting if anything else (another
  // compositor, or other TBB users in the process) still holds the
  // scheduler; in that case whoever releases it last has to wait
  tbb::finalize(m_tbbHandle, std::nothrow);
}

CPUCompositor::Range CPUCompositor::rangeOfPeer(int peer)
{
  int numPixels = m_size.x * m_size.y;
  int numChunks = divRoundUp(numPixels, (int)CHUNK_SIZE);
  int chunk_begin = (peer * numChunks) / m_mpi.size;
  int chunk_end = ((peer + 1) * numChunks) / m_mpi.size;
  return Range(
      chunk_begin * CHUNK_SIZE, std::min(chunk_end * CHUNK_SIZE, numPixels));
}

void CPUCompositor::resize(int sx, int sy, ColorFormat format)
{
  assert(sx > 0);
  assert(sy > 0);
  m_size = {sx, sy};
  m_format = format;
  m_sized = false;

  // Not collective: every rank computes the same layout on its own, and
  // ptc has the ranks agree that all of them resized before compositing.
  Range myRange = rangeOfPeer(m_mpi.rank);
  int recvBufSize = myRange.size() * m_mpi.size;
  const int elems = elementsPerPixel(format);
  m_pixelType = OwnedDatatype(elems, elementType(format));
  m_p2p.recvBuf.color.resize(size_t(elems) * recvBufSize);
  m_p2p.recvBuf.depth.resize(recvBufSize);
  m_finalGather.anyRank.sendCount = myRange.size();
  m_finalGather.anyRank.sendBuf.color.resize(size_t(elems) * myRange.size());
  m_finalGather.anyRank.sendBuf.depth.resize(myRange.size());

  if (m_mpi.rank == 0) {
    /* compute receive offsets into FINAL frame buffer (Only need
       that on rank 0, but anyway...) */
    for (int peer = 0; peer < m_mpi.size; peer++) {
      Range peerRange = rangeOfPeer(peer);
      m_finalGather.rank0.recvOffset[peer] = peerRange.begin;
      m_finalGather.rank0.recvCount[peer] = peerRange.size();
    }
  }

  { /* compute receive offsets into peer-to-peer exchange state */
    int recvOffset = 0;
    int sendOffset = 0;
    for (int peer = 0; peer < m_mpi.size; peer++) {
      Range peerRange = rangeOfPeer(peer);

      m_p2p.sendOffset[peer] = sendOffset;
      m_p2p.sendCount[peer] = peerRange.size();

      m_p2p.recvOffset[peer] = recvOffset;
      m_p2p.recvCount[peer] = myRange.size();

      recvOffset += m_p2p.recvCount[peer];
      sendOffset += m_p2p.sendCount[peer];
    }
  }
  m_sized = true; // only once the layout is complete
}

/*! blends each of my pixels' fragments (one per rank, from the p2p
    receive buffers) into the final gather's send buffers */
template <typename PIXEL_T>
void CPUCompositor::compositeMyRange()
{
  using Element = typename PIXEL_T::Element;
  const Element *colors = (const Element *)m_p2p.recvBuf.color.data();
  Element *outColors = (Element *)m_finalGather.anyRank.sendBuf.color.data();
  const float *depths = m_p2p.recvBuf.depth.data();
  float *outDepths = m_finalGather.anyRank.sendBuf.depth.data();

  const size_t numPixels = (size_t)rangeOfPeer(m_mpi.rank).size();
  // rank r's fragment of pixel i is at [i + r*numPixels]
  const size_t rankStep = numPixels;
  using Range1D = tbb::blocked_range<size_t>;
  tbb::parallel_for(
      Range1D(0, numPixels, BLOCK_SIZE), [&](const Range1D &range) {
        std::vector<Fragment> fragments(m_mpi.size);
        for (size_t it = range.begin(); it < range.end(); it++) {
          for (size_t r = 0; r < fragments.size(); r++) {
            float depth = depths[it + r * rankStep];
            if (!(depth < NO_HIT_DEPTH))
              depth = NO_HIT_DEPTH;
            fragments[r] = {depth, (uint32_t)r};
          }
          std::sort(fragments.begin(), fragments.end());

          // back to front: each fragment goes over what is behind it;
          // colors are premultiplied, so rgb and alpha blend alike
          math::float4 composited =
              PIXEL_T::load(colors, it + fragments.back().rank * rankStep);
          for (size_t i = fragments.size() - 1; i-- > 0;) {
            const math::float4 frag =
                PIXEL_T::load(colors, it + fragments[i].rank * rankStep);
            composited = frag + (1.f - frag.w) * composited;
          }

          float final_depth = fragments.front().depth;
          if (final_depth >= NO_HIT_DEPTH)
            final_depth = std::numeric_limits<float>::infinity();

          PIXEL_T::store(outColors, it, composited);
          outDepths[it] = final_depth;
        }
      });
}

/*! out_color and out_depth should be null on all ranks > 0 */
void CPUCompositor::run(void *out_color,
    float *out_depth,
    const void *in_color,
    const float *in_depth)
{
  // ------------------------------------------------------------------
  // peer-to-peer stage
  // ------------------------------------------------------------------
  MPI_Alltoallv(in_color,
      m_p2p.sendCount.data(),
      m_p2p.sendOffset.data(),
      m_pixelType.get(),
      m_p2p.recvBuf.color.data(),
      m_p2p.recvCount.data(),
      m_p2p.recvOffset.data(),
      m_pixelType.get(),
      m_mpi.comm.get());
  MPI_Alltoallv(in_depth,
      m_p2p.sendCount.data(),
      m_p2p.sendOffset.data(),
      MPI_FLOAT,
      m_p2p.recvBuf.depth.data(),
      m_p2p.recvCount.data(),
      m_p2p.recvOffset.data(),
      MPI_FLOAT,
      m_mpi.comm.get());

  // ------------------------------------------------------------------
  // local compositing stage
  // ------------------------------------------------------------------
  switch (m_format) {
  case ColorFormat::RGBA32F:
    compositeMyRange<RGBA32FPixel>();
    break;
  case ColorFormat::RGBA8_SRGB:
    compositeMyRange<RGBA8SRGBPixel>();
    break;
  case ColorFormat::RGBA8:
    compositeMyRange<RGBA8Pixel>();
    break;
  }

  // ------------------------------------------------------------------
  // final gather stage
  // ------------------------------------------------------------------
  // every rank sends its range; rank 0's receive arguments are the only
  // ones used, so ranks > 0 pass their (empty) rank 0 tables and null out_*
  MPI_Gatherv(m_finalGather.anyRank.sendBuf.color.data(),
      m_finalGather.anyRank.sendCount,
      m_pixelType.get(),
      out_color,
      m_finalGather.rank0.recvCount.data(),
      m_finalGather.rank0.recvOffset.data(),
      m_pixelType.get(),
      0,
      m_mpi.comm.get());
  MPI_Gatherv(m_finalGather.anyRank.sendBuf.depth.data(),
      m_finalGather.anyRank.sendCount,
      MPI_FLOAT,
      out_depth,
      m_finalGather.rank0.recvCount.data(),
      m_finalGather.rank0.recvOffset.data(),
      MPI_FLOAT,
      0,
      m_mpi.comm.get());
}

} // namespace anari_ptc
