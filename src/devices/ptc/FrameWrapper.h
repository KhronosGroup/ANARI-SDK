// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// ptc
#include "CPUCompositor.h"
#include "IdCompositor.h"
#include "Status.h"
// anari
#include <anari/anari.h>
#include <anari/anari_cpp/ext/linalg.h>
// helium
#include <helium/utility/IntrusivePtr.h>
#include <helium/utility/ParameterizedObject.h>
// mpi
#include <mpi.h>
// std
#include <array>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace anari_ptc {

struct PTCDevice;

struct FrameWrapper : public helium::RefCounted, helium::ParameterizedObject
{
  /*! ptcDevice is the device that owns this frame (its handle is passed
      to the app's frame completion callback, and it receives status
      reports and the notice that this frame is destroyed); d and f are
      the wrapped device and frame */
  FrameWrapper(PTCDevice *ptcDevice, ANARIDevice d, ANARIFrame f);
  ~FrameWrapper() override;

  ANARIFrame handle() const;

  // Forward generic ANARI API calls to underlying ANARIFrame /////////////////
  void setParameter(const char *name, ANARIDataType type, const void *mem);
  void unsetParameter(const char *name);
  void unsetAllParameters();
  void *mapParameterArray1D(const char *name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t *elementStride);
  void *mapParameterArray2D(const char *name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t numElements2,
      uint64_t *elementStride);
  void *mapParameterArray3D(const char *name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t numElements2,
      uint64_t numElements3,
      uint64_t *elementStride);
  void unmapParameterArray(const char *name);
  void commitParameters();
  void release();
  void retain();
  int getProperty(const char *name,
      ANARIDataType type,
      void *mem,
      uint64_t size,
      ANARIWaitMask mask);
  const void *frameBufferMap(const char *channel,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType);
  void frameBufferUnmap(const char *channel);
  /*! renders and composites, reports a failure through the status callback
      (it does not throw one), then calls the app's frame completion
      callback, if set: after every render, failed or not. Throws only if
      called from inside that callback, without calling it again. */
  void renderFrame();
  int frameReady(ANARIWaitMask);
  void discardFrame();
  /////////////////////////////////////////////////////////////////////////////

 private:
  // The frame parameters ptc tracks, with the defaults the wrapped frame
  // uses when they are unset or set with the wrong type.
  struct TrackedParameters
  {
    anari::math::uint2 size{0u, 0u};
    ANARIDataType colorType{ANARI_UNKNOWN};
    // Whether the *app* enabled channel.depth, which decides whether the
    // composited depth may be mapped. The wrapped frame always renders
    // depth, since compositing needs it.
    bool depthEnabled{false};
    // Whether the app enabled each of ID_CHANNELS (as ANARI_UINT32). Unlike
    // depth, they are forwarded: the wrapped frame renders them only then.
    std::array<bool, NUM_ID_CHANNELS> idEnabled{};
    // The app's frame completion callback. Never forwarded: the wrapped
    // frame completes before compositing, and would pass its own handles.
    ANARIFrameCompletionCallback completionCallback{nullptr};
    const void *completionCallbackUserData{nullptr};
  };

  /*! What a successful render produced, as of the parameters committed when
      it ran: the buffers and which channels may be mapped. Built from
      scratch by renderFrame(), which keeps it in m_rendered only once the
      render has succeeded. */
  struct RenderedFrame
  {
    anari::math::uint2 size{0u, 0u};
    // With no color type the compositor still runs, on transparent black
    // input, so depth composites.
    ANARIDataType colorType{ANARI_UNKNOWN};
    // m_committed.depthEnabled: whether the composited depth may be mapped
    bool depthEnabled{false};
    // m_committed.idEnabled, likewise
    std::array<bool, NUM_ID_CHANNELS> idEnabled{};
    // composited ID_CHANNELS; empty unless enabled
    std::array<std::vector<uint32_t>, NUM_ID_CHANNELS> ids;
    // composited color and depth (rank 0); empty without a non-empty size.
    // color is RGBA8 (one word per pixel) or RGBA32F (four words per pixel)
    std::vector<float> depth;
    std::vector<uint32_t> color;
    // compositor color input when the frame has no channel.color
    std::vector<uint32_t> noColor;
  };

  /*! A channel of the wrapped frame, mapped for as long as this lives. */
  class MappedChannel
  {
   public:
    MappedChannel() = default;
    /*! takes over 'data', as returned by anariMapFrame() of 'channel' */
    MappedChannel(
        ANARIDevice d, ANARIFrame f, const char *channel, const void *data);
    ~MappedChannel();
    MappedChannel(MappedChannel &&o) noexcept;
    MappedChannel &operator=(MappedChannel &&o) noexcept;
    MappedChannel(const MappedChannel &) = delete;
    MappedChannel &operator=(const MappedChannel &) = delete;
    /*! unmaps now, if mapped */
    void reset();
    const void *data() const;

   private:
    ANARIDevice m_device{nullptr};
    ANARIFrame m_frame{nullptr};
    const char *m_channel{nullptr};
    const void *m_data{nullptr};
  };

  /*! The outcome of renderLocal(), on this rank. */
  struct LocalRender
  {
    RenderedFrame frame;
    // Declared so that they unmap in the order depth, color. They stay
    // mapped through the collective step, which reads them in place.
    MappedChannel color;
    MappedChannel depth;
    // set if this rank failed before compositing
    std::exception_ptr failure;
  };

  /*! renders and composites into m_rendered; throws if the render failed on
      any rank */
  void renderAndComposite();
  /*! everything before compositing: not collective, and so may fail on
      this rank alone. With 'compositing' that is captured in the result's
      'failure' (for compositeCollective() to have all ranks skip the
      composite); without, it is thrown. */
  LocalRender renderLocal(
      std::optional<RenderedFrame> &&previous, bool compositing);
  /*! has the ranks agree that none failed in renderLocal(), then composites
      (the only other collective steps), and unmaps the channels. Throws
      this rank's failure, or that another rank's failed. */
  void compositeCollective(LocalRender &local);
  /*! calls the app's frame completion callback, if set */
  void completeFrame();
  /*! throws if called from inside this frame's completion callback;
      'what' is the refused operation, e.g. "render" */
  void refuseFromCallback(const char *what) const;

  /*! the frame to render into: 'previous' (the last render's, emptied) if
      it was made for the committed size and color type, else a new one
      with the compositor resized for it. Not collective: renderFrame() has
      the ranks agree afterwards. */
  RenderedFrame makeFrame(std::optional<RenderedFrame> &&previous);
  /*! whether the compositor has to be resized for the next render: it is
      new since the last one (or missing) */
  bool compositorNeedsResize(
      anari::math::uint2 size, ANARIDataType colorType) const;
  /*! the local half of id compositing: stages the enabled ID_CHANNELS
      read from the wrapped frame, with 'depth' (its channel.depth) */
  void stageIds(const RenderedFrame &frame, const float *depth);
  /*! whether 'ok' is true on every rank of 'comm'; collective over
      'comm'. Ranks call it before a collective step that may have failed
      on some of them only, so that all of them skip that step (and
      report an error) instead of the others hanging in it. */
  static bool agree(MPI_Comm comm, bool ok);
  /*! maps a channel of the wrapped frame; throws unless it is 'type' and
      'size' */
  MappedChannel mapWrapped(
      const char *channel, ANARIDataType type, anari::math::uint2 size);
  /*! makes the compositor composite over 'comm' (a copy of it), if it
      doesn't already composite over the same ranks; collective over
      'comm' and over the communicator it replaces. MPI_COMM_NULL only
      tears down the current compositor. */
  void setCommunicator(MPI_Comm comm);

  /*! reports what is wrong with the tracked parameters set since the last
      commit (e.g. a wrong type), once, in order of parameter name */
  void reportParametersSetSinceCommit();
  /*! the tracked parameters as currently set; a value of the wrong type
      reads as unset, as it does for the wrapped frame (helium's getParam) */
  TrackedParameters readTrackedParameters() const;

  /*! makes the wrapped frame render depth, which compositing needs
      whatever the app enabled */
  void enableWrappedDepth();
  /*! reports through the status callback, with ANARI_STATUS_NO_ERROR
      below ERROR and ANARI_STATUS_UNKNOWN_ERROR from ERROR up */
  void reportStatus(ANARIStatusSeverity severity, const std::string &msg) const;
  // whether completeFrame() is calling the app's callback right now
  bool m_inCompletionCallback{false};

  // composites color and depth over the duplicate of the committed
  // communicator it owns (so the app may free its own); null before the
  // first commit and while it is MPI_COMM_NULL. Rank 0 of that
  // communicator receives the composited image.
  std::unique_ptr<CPUCompositor> m_compositor;

  PTCDevice *m_ptcDevice{nullptr};
  ANARIDevice m_device{nullptr};
  ANARIFrame m_frame{nullptr};

  // The tracked parameters are stored in this ParameterizedObject (only
  // those: other parameters just go to the wrapped frame), and
  // commitParameters() reads them into m_committed, as the wrapped frame does
  // with its own parameters.
  TrackedParameters m_committed;
  // The tracked parameters set since the last commit, with the type each was
  // set as, for the next commit to report on; unsetting one drops it.
  std::map<std::string, ANARIDataType, std::less<>> m_setSinceCommit;
  // Whether 'mpiCommunicator' was set or unset since the last commit, for
  // the next commit to read it; true at first, so that the first commit
  // creates the compositor.
  bool m_commChanged{true};

  // The last render, if it succeeded; empty before the first and after a
  // failed one, when nothing may be mapped.
  std::optional<RenderedFrame> m_rendered;
  // what stageIds() staged, for compositing the ids into m_rendered's
  IdCompositor m_idCompositor;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline FrameWrapper::MappedChannel::~MappedChannel()
{
  reset();
}

inline const void *FrameWrapper::MappedChannel::data() const
{
  return m_data;
}

// Helper functions ///////////////////////////////////////////////////////////

/*! bytes per pixel of the composited color buffer; without a color type
    (ANARI_UNKNOWN) the compositor still works on 32-bit RGBA */
inline size_t colorPixelSize(ANARIDataType colorType)
{
  return colorType == ANARI_FLOAT32_VEC4 ? 4 * sizeof(float) : sizeof(uint32_t);
}

/*! whether the compositor handles this color type; ANARI_UNKNOWN (no
    color) counts, the compositor then runs on transparent black */
inline bool isCompositableColorType(ANARIDataType colorType)
{
  return colorType == ANARI_UNKNOWN || colorType == ANARI_UFIXED8_VEC4
      || colorType == ANARI_UFIXED8_RGBA_SRGB
      || colorType == ANARI_FLOAT32_VEC4;
}

} // namespace anari_ptc
