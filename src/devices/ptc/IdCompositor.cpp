// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// ptc
#include "IdCompositor.h"
// std
#include <climits>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace anari_ptc {

namespace {

// composite() reduces one record of 32-bit words per pixel: the depth
// (as float bits), the rank, then the id of each enabled channel
constexpr size_t RECORD_DEPTH = 0;
constexpr size_t RECORD_RANK = 1;
constexpr size_t RECORD_FIRST_ID = 2;

float recordDepth(const uint32_t *record)
{
  float depth;
  std::memcpy(&depth, &record[RECORD_DEPTH], sizeof(depth));
  // NaN never wins over a hit
  return std::isnan(depth) ? INFINITY : depth;
}

/*! MPI_Op: keeps the nearer of each pair of records, the lower rank's at
    equal depth (as the compositor orders equal-depth fragments), which
    makes it commutative */
void nearestRecord(void *in, void *inout, int *len, MPI_Datatype *type)
{
  int bytes = 0;
  MPI_Type_size(*type, &bytes);
  const size_t words = size_t(bytes) / sizeof(uint32_t);
  auto *a = (const uint32_t *)in;
  auto *b = (uint32_t *)inout;
  for (int i = 0; i < *len; i++, a += words, b += words) {
    const float da = recordDepth(a);
    const float db = recordDepth(b);
    if (da < db || (da == db && a[RECORD_RANK] < b[RECORD_RANK]))
      std::memcpy(b, a, bytes);
  }
}

} // namespace

void IdCompositor::stage(int rank,
    anari::math::uint2 size,
    const float *depth,
    const std::array<bool, NUM_ID_CHANNELS> &enabled,
    const ReadChannel &read)
{
  reset();
  for (size_t i = 0; i < NUM_ID_CHANNELS; i++) {
    if (enabled[i])
      m_channels.push_back(i);
  }
  if (m_channels.empty())
    return;

  // the same on every rank
  const size_t numPixels = size_t(size.x) * size.y;
  if (numPixels > size_t(INT_MAX)) {
    m_channels.clear();
    throw std::runtime_error(
        "cannot composite id channels: the frame has too many pixels");
  }
  m_rank = rank;
  m_numPixels = numPixels;
  const size_t words = RECORD_FIRST_ID + m_channels.size();
  m_records.resize(numPixels * words);

  for (size_t p = 0; p < numPixels; p++) {
    uint32_t *record = m_records.data() + p * words;
    std::memcpy(&record[RECORD_DEPTH], &depth[p], sizeof(float));
    record[RECORD_RANK] = uint32_t(rank);
  }
  for (size_t c = 0; c < m_channels.size(); c++)
    read(m_channels[c], m_records.data() + RECORD_FIRST_ID + c, words);
}

void IdCompositor::composite(MPI_Comm comm, IdArrays &ids)
{
  if (m_channels.empty())
    return;
  const size_t numPixels = m_numPixels;
  const size_t words = RECORD_FIRST_ID + m_channels.size();
  auto &records = m_records;

  MPI_Datatype recordType;
  MPI_Type_contiguous(int(words), MPI_UINT32_T, &recordType);
  MPI_Type_commit(&recordType);
  MPI_Op op;
  MPI_Op_create(&nearestRecord, 1, &op);
  // on rank 0, in place: 'records' becomes the nearest record of every rank
  MPI_Reduce(m_rank == 0 ? MPI_IN_PLACE : records.data(),
      m_rank == 0 ? records.data() : nullptr,
      int(numPixels),
      recordType,
      op,
      0,
      comm);
  MPI_Op_free(&op);
  MPI_Type_free(&recordType);

  // on other ranks, this rank's own ids (the contents are undefined there)
  for (size_t c = 0; c < m_channels.size(); c++) {
    auto &out = ids[m_channels[c]];
    out.resize(numPixels);
    for (size_t p = 0; p < numPixels; p++)
      out[p] = records[p * words + RECORD_FIRST_ID + c];
  }
  reset();
}

void IdCompositor::reset()
{
  m_numPixels = 0;
  m_channels.clear();
  m_records = {};
}

} // namespace anari_ptc
