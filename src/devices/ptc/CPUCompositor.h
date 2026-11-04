// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include <anari/anari_cpp/ext/linalg.h>
// mpi
#include <mpi.h>
// tbb
#include <tbb/global_control.h>
// std
#include <cstdint>
#include <vector>

namespace anari_ptc {
using namespace anari;

/*! An MPI_Comm that is freed on destruction, unless MPI was finalized
    meanwhile (freeing is then an error, and the library has released it
    already). Move-only; MPI_COMM_NULL when empty. */
class OwnedComm
{
 public:
  OwnedComm() = default;
  /*! duplicates 'comm' (collective over it); MPI_COMM_NULL stays empty */
  static OwnedComm dup(MPI_Comm comm);
  OwnedComm(OwnedComm &&o) noexcept;
  OwnedComm &operator=(OwnedComm &&o) noexcept;
  OwnedComm(const OwnedComm &) = delete;
  OwnedComm &operator=(const OwnedComm &) = delete;
  ~OwnedComm();

  MPI_Comm get() const;
  void reset();

 private:
  MPI_Comm m_comm{MPI_COMM_NULL};
};

/*! A committed MPI datatype of 'count' contiguous 'element's, freed on
    destruction unless MPI was finalized meanwhile (the library has released
    it already). Move-only; MPI_DATATYPE_NULL when empty. */
class OwnedDatatype
{
 public:
  OwnedDatatype() = default;
  OwnedDatatype(int count, MPI_Datatype element);
  OwnedDatatype(OwnedDatatype &&o) noexcept;
  OwnedDatatype &operator=(OwnedDatatype &&o) noexcept;
  OwnedDatatype(const OwnedDatatype &) = delete;
  OwnedDatatype &operator=(const OwnedDatatype &) = delete;
  ~OwnedDatatype();

  MPI_Datatype get() const;
  void reset();

 private:
  MPI_Datatype m_type{MPI_DATATYPE_NULL};
};

struct CPUCompositor
{
  /*! pixel format of the color the compositor reads and writes */
  enum class ColorFormat
  {
    RGBA8, /*!< 4x8 bit in one uint32_t, r in the low byte */
    RGBA8_SRGB, /*!< as RGBA8, with sRGB-encoded rgb (alpha linear) */
    RGBA32F /*!< 4 floats, r first */
  };

  /*! composites over 'comm', which this takes over (and frees after the
      compositor is gone) only if the constructor returns; if it throws,
      'comm' is left with the caller */
  CPUCompositor(OwnedComm &comm);
  ~CPUCompositor();

  /*! the communicator composited over, and this rank in it (rank 0
      receives the composited image) */
  MPI_Comm comm() const;
  int rank() const;
  int numRanks() const;
  /*! whether resize() was last called with these arguments */
  bool isSizedFor(int sx, int sy, ColorFormat f) const;

  void resize(int sx, int sy, ColorFormat format);
  /*! out_color and out_depth should be null on all ranks > 0; colors
      are in the format given to resize(), with premultiplied rgb;
      fragments are blended with premultiplied "over", in linear space
      (RGBA8_SRGB is decoded before and encoded after) */
  void run(void *out_color,
      float *out_depth,
      const void *in_color,
      const float *in_depth);
  struct Range
  {
    Range() = default;
    Range(int begin, int end) : begin(begin), end(end) {}
    int size() const;
    int begin = 0, end = 0;
  };

 private:
  math::int2 m_size{0, 0};
  bool m_sized{false};
  ColorFormat m_format{ColorFormat::RGBA8};

  Range rangeOfPeer(int peer);
  /*! the local compositing stage, for the color type of 'format' */
  template <typename PIXEL_T>
  void compositeMyRange();

  /*! one pixel of color in 'format': a uint32_t, or four floats; so
      counts and offsets of color and depth are both in pixels */
  OwnedDatatype m_pixelType;

  /*! received/sent colors, as 'format' packs a pixel into 4 or 16 bytes
      (uint32_t elements, whatever the format's own element type) */
  struct
  {
    struct
    {
      std::vector<uint32_t> color;
      std::vector<float> depth;
    } recvBuf;
    Range myRange;
    // in pixels, for color and depth
    std::vector<int> recvOffset;
    std::vector<int> recvCount;
    std::vector<int> sendOffset;
    std::vector<int> sendCount;
  } m_p2p;
  struct
  {
    struct
    {
      std::vector<int> recvOffset;
      std::vector<int> recvCount;
    } rank0;
    struct
    {
      int sendCount;
      struct
      {
        std::vector<uint32_t> color;
        std::vector<float> depth;
      } sendBuf;
    } anyRank;
  } m_finalGather;

  struct
  {
    OwnedComm comm;
    int size;
    int rank;
  } m_mpi;
  /*! keeps the TBB scheduler alive while this compositor exists; on
      destruction we wait for TBB's worker threads to exit, so they
      can't outlive the (dlclose'able) device library that loaded TBB */
  tbb::task_scheduler_handle m_tbbHandle{tbb::attach{}};

  static constexpr int CHUNK_SIZE = 64;
  static constexpr int BLOCK_SIZE = 16 * CHUNK_SIZE;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline OwnedComm::OwnedComm(OwnedComm &&o) noexcept : m_comm(o.m_comm)
{
  o.m_comm = MPI_COMM_NULL;
}

inline OwnedComm &OwnedComm::operator=(OwnedComm &&o) noexcept
{
  if (this != &o) {
    reset();
    m_comm = o.m_comm;
    o.m_comm = MPI_COMM_NULL;
  }
  return *this;
}

inline OwnedComm::~OwnedComm()
{
  reset();
}

inline MPI_Comm OwnedComm::get() const
{
  return m_comm;
}

inline OwnedDatatype::OwnedDatatype(OwnedDatatype &&o) noexcept
    : m_type(o.m_type)
{
  o.m_type = MPI_DATATYPE_NULL;
}

inline OwnedDatatype &OwnedDatatype::operator=(OwnedDatatype &&o) noexcept
{
  if (this != &o) {
    reset();
    m_type = o.m_type;
    o.m_type = MPI_DATATYPE_NULL;
  }
  return *this;
}

inline OwnedDatatype::~OwnedDatatype()
{
  reset();
}

inline MPI_Datatype OwnedDatatype::get() const
{
  return m_type;
}

inline int CPUCompositor::Range::size() const
{
  return end - begin;
}

inline MPI_Comm CPUCompositor::comm() const
{
  return m_mpi.comm.get();
}

inline int CPUCompositor::rank() const
{
  return m_mpi.rank;
}

inline int CPUCompositor::numRanks() const
{
  return m_mpi.size;
}

inline bool CPUCompositor::isSizedFor(int sx, int sy, ColorFormat f) const
{
  return m_size.x == sx && m_size.y == sy && m_format == f && m_sized;
}

} // namespace anari_ptc
