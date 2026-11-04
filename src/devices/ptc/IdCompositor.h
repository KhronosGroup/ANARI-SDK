// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include <anari/anari_cpp/ext/linalg.h>
// mpi
#include <mpi.h>
// std
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace anari_ptc {

/*! Frame channels ptc composites by taking the value of each pixel's
    nearest fragment (equal depth: the lower rank's), since ids don't
    blend. Each is UINT32 and requested from the wrapped frame only when
    the app enables it. */
inline constexpr const char *ID_CHANNELS[] = {
    "channel.primitiveId", "channel.objectId", "channel.instanceId"};
inline constexpr size_t NUM_ID_CHANNELS =
    sizeof(ID_CHANNELS) / sizeof(ID_CHANNELS[0]);

/*! Composites the enabled ID_CHANNELS: stage() gathers this rank's record
    of each pixel (depth, rank, ids), composite() keeps the nearest record
    of every rank with one reduction. The staged records are scratch, kept
    here between the two (and emptied by composite() and reset()). */
class IdCompositor
{
 public:
  using IdArrays = std::array<std::vector<uint32_t>, NUM_ID_CHANNELS>;

  /*! the id of a pixel with no hit */
  static constexpr uint32_t NO_HIT = ~0u;

  /*! writes the ids of ID_CHANNELS[channel] to dst[p * stride], for each
      pixel p (NO_HIT where the channel has none) */
  using ReadChannel =
      std::function<void(size_t channel, uint32_t *dst, size_t stride)>;

  /*! stages this rank's (rank 'rank' of the communicator) records of
      'size' pixels, with 'depth' as their depths, for the channels enabled
      in 'enabled'; 'read' provides the ids. Throws if the frame has too
      many pixels. Local to this rank (not collective). */
  void stage(int rank,
      anari::math::uint2 size,
      const float *depth,
      const std::array<bool, NUM_ID_CHANNELS> &enabled,
      const ReadChannel &read);

  /*! reduces the staged records over 'comm' and stores each staged
      channel's ids in 'ids' (rank 0's are the composited ones; on other
      ranks, this rank's own). Collective over 'comm', unless nothing is
      staged. */
  void composite(MPI_Comm comm, IdArrays &ids);

  /*! drops what was staged */
  void reset();

 private:
  int m_rank{0};
  size_t m_numPixels{0};
  // the staged ID_CHANNELS (indices) and one record per pixel
  std::vector<size_t> m_channels;
  std::vector<uint32_t> m_records;
};

} // namespace anari_ptc
