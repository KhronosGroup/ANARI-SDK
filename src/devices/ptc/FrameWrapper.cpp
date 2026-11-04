// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// ptc
#include "FrameWrapper.h"
#include "PTCDevice.h"
#include "ScopedFlag.h"
// anari
#include <anari/frontend/type_utility.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <array>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace anari_ptc {

// Helper functions ///////////////////////////////////////////////////////////

namespace {

/*! index of 'name' in ID_CHANNELS, or -1 */
int idChannelIndex(std::string_view name)
{
  for (size_t i = 0; i < NUM_ID_CHANNELS; i++) {
    if (name == ID_CHANNELS[i])
      return int(i);
  }
  return -1;
}

/*! the compositor's color format for a compositable color type; RGBA8
    without one (ANARI_UNKNOWN) */
CPUCompositor::ColorFormat colorFormat(ANARIDataType colorType)
{
  if (colorType == ANARI_FLOAT32_VEC4)
    return CPUCompositor::ColorFormat::RGBA32F;
  if (colorType == ANARI_UFIXED8_RGBA_SRGB)
    return CPUCompositor::ColorFormat::RGBA8_SRGB;
  return CPUCompositor::ColorFormat::RGBA8;
}

/*! 'type', and the type it names if it is ANARI_DATA_TYPE, for messages */
std::string describeType(ANARIDataType type, const void *mem)
{
  std::string result = anari::toString(type);
  if (type == ANARI_DATA_TYPE && mem)
    result += std::string(" ") + anari::toString(*(const ANARIDataType *)mem);
  return result;
}

/*! the ANARI_DATA_TYPE parameter 'name' of 'o'; ANARI_UNKNOWN if it is
    unset or of another type */
ANARIDataType dataTypeParam(
    const helium::ParameterizedObject &o, const std::string &name)
{
  ANARIDataType value = ANARI_UNKNOWN;
  o.getParam(name, ANARI_DATA_TYPE, &value);
  return value;
}

/*! A frame parameter ptc tracks. The next commit warns about one set with
    another type than 'type', saying it is 'expected' and what happens
    instead ('effect'). */
struct TrackedParameter
{
  std::string_view name;
  ANARIDataType type;
  // for a channel, the ANARI_DATA_TYPE value that enables it: the next
  // commit also warns about any other but ANARI_UNKNOWN; else ANARI_UNKNOWN
  ANARIDataType channelType;
  // whether it is also forwarded to the wrapped frame
  bool forwarded;
  const char *expected;
  const char *effect;
};

constexpr TrackedParameter TRACKED_PARAMETERS[] = {
    {"size",
        ANARI_UINT32_VEC2,
        ANARI_UNKNOWN,
        true,
        "ANARI_UINT32_VEC2",
        "treated as unset (0x0)"},
    // An unsupported color type is kept (and the next commit reports an
    // error), so that the frame fails to render on this rank, and the ranks
    // agree to fail together.
    {"channel.color",
        ANARI_DATA_TYPE,
        ANARI_UNKNOWN,
        true,
        "an ANARI_DATA_TYPE",
        "treated as unset"},
    // Never forwarded: the wrapped frame keeps rendering FLOAT32 depth for
    // compositing. Only FLOAT32 is a valid depth type (as in helide).
    {"channel.depth",
        ANARI_DATA_TYPE,
        ANARI_FLOAT32,
        false,
        "ANARI_DATA_TYPE ANARI_FLOAT32",
        "depth is not composited"},
    // Never forwarded: renderFrame() calls it after compositing.
    {"frameCompletionCallback",
        ANARI_FRAME_COMPLETION_CALLBACK,
        ANARI_UNKNOWN,
        false,
        "ANARI_FRAME_COMPLETION_CALLBACK",
        "treated as unset"},
    {"frameCompletionCallbackUserData",
        ANARI_VOID_POINTER,
        ANARI_UNKNOWN,
        false,
        "ANARI_VOID_POINTER",
        "treated as unset (null)"},
    // Never forwarded: ptc implements KHR_DATA_PARALLEL_MPI. The app passes
    // a pointer to an MPI_Comm as the ANARI_VOID_POINTER value, which works
    // whether MPI_Comm is an int (MPICH) or a pointer (Open MPI). It is read
    // at the next commit, so it only has to stay valid until that commit
    // returns. A null pointer or a wrong type reads as unset
    // (MPI_COMM_WORLD), which on some ranks only would hang: hence the
    // warning.
    {"mpiCommunicator",
        ANARI_VOID_POINTER,
        ANARI_UNKNOWN,
        false,
        "ANARI_VOID_POINTER (a pointer to an MPI_Comm)",
        "MPI_COMM_WORLD is used"},
};

// Each of ID_CHANNELS; forwarded, as the wrapped frame renders the ids ptc
// composites.
constexpr TrackedParameter ID_CHANNEL_PARAMETER{"",
    ANARI_DATA_TYPE,
    ANARI_UINT32,
    true,
    "ANARI_DATA_TYPE ANARI_UINT32",
    "the channel is not composited"};

/*! the frame parameter 'name' if ptc tracks it, else null */
const TrackedParameter *findTracked(std::string_view name)
{
  for (const auto &t : TRACKED_PARAMETERS) {
    if (t.name == name)
      return &t;
  }
  return idChannelIndex(name) >= 0 ? &ID_CHANNEL_PARAMETER : nullptr;
}

} // namespace

// FrameWrapper definitions ///////////////////////////////////////////////////

FrameWrapper::FrameWrapper(PTCDevice *ptcDevice, ANARIDevice d, ANARIFrame f)
    : m_ptcDevice(ptcDevice), m_device(d), m_frame(f)
{
  anariRetain(m_device, m_device);
  enableWrappedDepth();
}

FrameWrapper::~FrameWrapper()
{
  // first, as before the wrapped device is released; it frees the
  // communicator it composites over (unless MPI was finalized)
  m_compositor.reset();
  anariRelease(m_device, m_frame);
  anariRelease(m_device, m_device);
  m_ptcDevice->removeFrameWrapper(this);
}

ANARIFrame FrameWrapper::handle() const
{
  return m_frame;
}

void FrameWrapper::setParameter(
    const char *name, ANARIDataType type, const void *mem)
{
  const TrackedParameter *t = findTracked(name);
  if (t) {
    // Object handles are the wrapped device's, which helium would
    // reference-count as its own: they are not stored, and read as unset.
    if (anari::isObject(type))
      removeParam(name);
    else
      setParam(name, type, mem);
    m_setSinceCommit[name] = type;
    m_commChanged |= t->name == "mpiCommunicator";
  }
  if (!t || t->forwarded)
    anariSetParameter(m_device, m_frame, name, type, mem);
}

void FrameWrapper::unsetParameter(const char *name)
{
  const TrackedParameter *t = findTracked(name);
  if (t) {
    removeParam(name);
    m_setSinceCommit.erase(name);
    m_commChanged |= t->name == "mpiCommunicator";
  }
  if (!t || t->forwarded)
    anariUnsetParameter(m_device, m_frame, name);
}

void FrameWrapper::unsetAllParameters()
{
  removeAllParams();
  m_setSinceCommit.clear();
  m_commChanged = true;
  anariUnsetAllParameters(m_device, m_frame);
  enableWrappedDepth();
}

void FrameWrapper::enableWrappedDepth()
{
  anari::setParameter(m_device, m_frame, "channel.depth", ANARI_FLOAT32);
}

void FrameWrapper::reportStatus(
    ANARIStatusSeverity severity, const std::string &msg) const
{
  m_ptcDevice->reportStatus(severity, defaultStatusCode(severity), msg.c_str());
}

void FrameWrapper::reportParametersSetSinceCommit()
{
  for (const auto &[name, type] : std::exchange(m_setSinceCommit, {})) {
    const TrackedParameter &t = *findTracked(name);
    const ANARIDataType value =
        type == ANARI_DATA_TYPE ? dataTypeParam(*this, name) : ANARI_UNKNOWN;
    if (type != t.type
        || (t.channelType != ANARI_UNKNOWN && value != t.channelType
            && value != ANARI_UNKNOWN)) {
      m_ptcDevice->reportStatus(ANARI_SEVERITY_WARNING,
          ANARI_STATUS_INVALID_ARGUMENT,
          ("frame parameter '" + name + "' was set as "
              + describeType(type, &value) + ", but must be " + t.expected
              + "; " + t.effect)
              .c_str());
    } else if (name == "channel.color" && !isCompositableColorType(value)) {
      m_ptcDevice->reportStatus(ANARI_SEVERITY_ERROR,
          ANARI_STATUS_INVALID_ARGUMENT,
          (std::string("cannot composite 'channel.color' of type ")
              + anari::toString(value)
              + " (supported are UFIXED8_VEC4, UFIXED8_RGBA_SRGB and "
                "FLOAT32_VEC4): the frame cannot be rendered until it is "
                "set to one of them or unset")
              .c_str());
    }
  }
}

FrameWrapper::TrackedParameters FrameWrapper::readTrackedParameters() const
{
  TrackedParameters p;
  p.size = getParam<anari::math::uint2>("size", p.size);
  p.colorType = dataTypeParam(*this, "channel.color");
  p.depthEnabled = dataTypeParam(*this, "channel.depth") == ANARI_FLOAT32;
  for (size_t i = 0; i < NUM_ID_CHANNELS; i++)
    p.idEnabled[i] = dataTypeParam(*this, ID_CHANNELS[i]) == ANARI_UINT32;
  p.completionCallback = getParam<ANARIFrameCompletionCallback>(
      "frameCompletionCallback", nullptr);
  p.completionCallbackUserData =
      getParam<const void *>("frameCompletionCallbackUserData", nullptr);
  return p;
}

void *FrameWrapper::mapParameterArray1D(const char *name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t *elementStride)
{
  return anariMapParameterArray1D(
      m_device, m_frame, name, dataType, numElements1, elementStride);
}

void *FrameWrapper::mapParameterArray2D(const char *name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t numElements2,
    uint64_t *elementStride)
{
  return anariMapParameterArray2D(m_device,
      m_frame,
      name,
      dataType,
      numElements1,
      numElements2,
      elementStride);
}

void *FrameWrapper::mapParameterArray3D(const char *name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t numElements2,
    uint64_t numElements3,
    uint64_t *elementStride)
{
  return anariMapParameterArray3D(m_device,
      m_frame,
      name,
      dataType,
      numElements1,
      numElements2,
      numElements3,
      elementStride);
}

void FrameWrapper::unmapParameterArray(const char *name)
{
  anariUnmapParameterArray(m_device, m_frame, name);
}

void FrameWrapper::commitParameters()
{
  refuseFromCallback("commit");
  reportParametersSetSinceCommit();
  m_committed = readTrackedParameters();
  anariCommitParameters(m_device, m_frame);

  if (!std::exchange(m_commChanged, false))
    return;
  // The app's MPI_Comm only has to stay valid until this commit returns.
  const auto *appComm =
      (const MPI_Comm *)getParam<const void *>("mpiCommunicator", nullptr);
  const MPI_Comm comm = appComm ? *appComm : MPI_COMM_WORLD;
  setCommunicator(comm);
  if (comm == MPI_COMM_NULL) {
    // e.g. a rank left out of an MPI_Comm_split; it still took part in
    // tearing down the old communicator above, with the ranks that stayed
    throw std::runtime_error(
        "'mpiCommunicator' points to MPI_COMM_NULL: the frame cannot be "
        "rendered until another communicator is committed");
  }
}

void FrameWrapper::setCommunicator(MPI_Comm comm)
{
  if (m_compositor) {
    int result = MPI_UNEQUAL;
    if (comm != MPI_COMM_NULL)
      MPI_Comm_compare(m_compositor->comm(), comm, &result);
    // same ranks in the same order: the compositor works as it is
    if (result == MPI_IDENT || result == MPI_CONGRUENT)
      return;
    // every rank of the old communicator commits a new one in lockstep, so
    // they all get here and free it together (the compositor owns it)
    m_compositor.reset();
  }
  if (comm == MPI_COMM_NULL)
    return;

  OwnedComm dup = OwnedComm::dup(comm);
  std::exception_ptr failure;
  try {
    // takes over 'dup' only if it succeeds
    m_compositor = std::make_unique<CPUCompositor>(dup);
    reportStatus(ANARI_SEVERITY_DEBUG,
        "CPU compositor on rank " + std::to_string(m_compositor->rank())
            + " of " + std::to_string(m_compositor->numRanks()));
  } catch (...) {
    failure = std::current_exception();
  }
  // If some ranks have no compositor, none may use this communicator: the
  // others would wait for them in every render.
  if (!agree(m_compositor ? m_compositor->comm() : dup.get(), !failure)) {
    m_compositor.reset(); // frees its communicator; 'dup' frees its own
    if (failure)
      std::rethrow_exception(failure);
    throw std::runtime_error(
        "another rank failed to create its compositor for this "
        "'mpiCommunicator': the frame cannot be rendered until a "
        "communicator is committed again");
  }
}

bool FrameWrapper::agree(MPI_Comm comm, bool ok)
{
  int mine = ok ? 1 : 0;
  int all = 0;
  MPI_Allreduce(&mine, &all, 1, MPI_INT, MPI_MIN, comm);
  return all != 0;
}

void FrameWrapper::release()
{
  refDec(helium::RefType::PUBLIC);
}

void FrameWrapper::retain()
{
  refInc(helium::RefType::PUBLIC);
}

int FrameWrapper::getProperty(const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    ANARIWaitMask mask)
{
  return anariGetProperty(m_device, m_frame, name, type, mem, size, mask);
}

const void *FrameWrapper::frameBufferMap(const char *_channel,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *pixelType)
{
  std::string_view channel = _channel;

  if (!m_rendered) {
    // e.g. channel.color rejected at commit: say so, not that it is unset
    reportStatus(ANARI_SEVERITY_WARNING,
        "cannot map '" + std::string(channel)
            + "': the frame has no successful render (it was not rendered, "
              "or its last render failed)");
  } else {
    const RenderedFrame &r = *m_rendered;
    // Report the buffers as of the last render: that is what they hold.
    *width = r.size.x;
    *height = r.size.y;

    // the channels ptc composites
    struct ChannelBuffer
    {
      std::string_view name;
      bool enabledAtRender;
      bool empty;
      const void *data;
      ANARIDataType type;
    };
    std::array<ChannelBuffer, NUM_ID_CHANNELS + 2> channels{
        ChannelBuffer{"channel.color",
            r.colorType != ANARI_UNKNOWN,
            r.color.empty(),
            r.color.data(),
            r.colorType},
        ChannelBuffer{"channel.depth",
            r.depthEnabled,
            r.depth.empty(),
            r.depth.data(),
            ANARI_FLOAT32}};
    for (size_t i = 0; i < NUM_ID_CHANNELS; i++) {
      channels[i + 2] = {ID_CHANNELS[i],
          r.idEnabled[i],
          r.ids[i].empty(),
          r.ids[i].data(),
          ANARI_UINT32};
    }

    const auto it = std::find_if(channels.begin(),
        channels.end(),
        [&](const ChannelBuffer &c) { return c.name == channel; });
    if (it != channels.end()) {
      if (it->enabledAtRender && !it->empty) {
        *pixelType = it->type;
        return it->data;
      }
      // channel.color is enabled by any color type, the others by their own
      const std::string without = it->name == "channel.color"
          ? "a 'channel.color' parameter"
          : "its '" + std::string(it->name) + "' parameter set to "
              + (it->type == ANARI_FLOAT32 ? "ANARI_FLOAT32" : "ANARI_UINT32");
      reportStatus(ANARI_SEVERITY_WARNING,
          "cannot map '" + std::string(channel) + "': "
              + (it->enabledAtRender
                      ? "the frame has not been rendered with a non-empty "
                        "'size'"
                      : "the frame was rendered without " + without));
    } else {
      // e.g. channel.normal and channel.albedo: they can't be blended, and
      // ptc doesn't composite them
      reportStatus(ANARI_SEVERITY_WARNING,
          "cannot map '" + std::string(channel)
              + "': ptc composites only channel.color, channel.depth, "
                "channel.primitiveId, channel.objectId and "
                "channel.instanceId");
    }
  }

  *width = 0;
  *height = 0;
  *pixelType = ANARI_UNKNOWN;
  return nullptr;
}

void FrameWrapper::frameBufferUnmap(const char *channel)
{
  // no-op
}

int FrameWrapper::frameReady(ANARIWaitMask m)
{
  return anariFrameReady(m_device, m_frame, m);
}

void FrameWrapper::discardFrame()
{
  anariDiscardFrame(m_device, m_frame);
}

FrameWrapper::RenderedFrame FrameWrapper::makeFrame(
    std::optional<RenderedFrame> &&previous)
{
  const auto size = m_committed.size;
  const auto colorType = m_committed.colorType;
  RenderedFrame frame;
  if (previous && previous->size == size && previous->colorType == colorType
      && !compositorNeedsResize(size, colorType)) {
    frame = std::move(*previous);
    frame.depthEnabled = m_committed.depthEnabled;
    frame.idEnabled = m_committed.idEnabled;
    for (size_t i = 0; i < NUM_ID_CHANNELS; i++) {
      if (!frame.idEnabled[i])
        frame.ids[i] = {};
    }
    return frame;
  }

  if (!isCompositableColorType(colorType)) {
    // rejected at commit already; renderFrame() has every rank skip
    // compositing if any rank throws
    throw std::runtime_error(std::string("cannot render: 'channel.color' "
                                         "of type ")
        + anari::toString(colorType)
        + " was rejected at commit (supported are UFIXED8_VEC4, "
          "UFIXED8_RGBA_SRGB and FLOAT32_VEC4)");
  }
  if (!m_compositor && size.x != 0 && size.y != 0) {
    throw std::runtime_error(
        "cannot composite: the frame has no valid "
        "'mpiCommunicator' committed");
  }
  frame.size = size;
  frame.colorType = colorType;
  frame.depthEnabled = m_committed.depthEnabled;
  frame.idEnabled = m_committed.idEnabled;

  if (size.x == 0 || size.y == 0)
    return frame;

  const size_t numPixels = size_t(size.x) * size.y;
  frame.color.assign(
      numPixels * (colorPixelSize(colorType) / sizeof(uint32_t)), 0u);
  frame.depth.assign(numPixels, 0.f);
  m_compositor->resize(size.x, size.y, colorFormat(colorType));
  if (colorType == ANARI_UNKNOWN)
    frame.noColor.assign(numPixels, 0u);
  return frame;
}

bool FrameWrapper::compositorNeedsResize(
    anari::math::uint2 size, ANARIDataType colorType) const
{
  // a compositor made since the buffers were allocated is not sized yet
  return size.x != 0 && size.y != 0
      && !(m_compositor
          && m_compositor->isSizedFor(size.x, size.y, colorFormat(colorType)));
}

void FrameWrapper::renderFrame()
{
  // refused without calling the callback, which would recurse
  refuseFromCallback("render");
  try {
    renderAndComposite();
  } catch (...) {
    // the app learns why the render failed before its callback runs
    m_ptcDevice->reportException("anariRenderFrame");
  }
  completeFrame();
}

void FrameWrapper::renderAndComposite()
{
  // m_rendered is set only once this render has succeeded
  auto previous = std::move(m_rendered);
  m_rendered.reset();
  m_idCompositor.reset();

  // Every rank sees the same commits, so all of them skip or composite.
  const auto size = m_committed.size;
  const bool compositing = m_compositor && size.x != 0 && size.y != 0;

  LocalRender local = renderLocal(std::move(previous), compositing);
  if (compositing)
    compositeCollective(local);
  m_rendered = std::move(local.frame);
}

FrameWrapper::LocalRender FrameWrapper::renderLocal(
    std::optional<RenderedFrame> &&previous, bool compositing)
{
  LocalRender local;
  try {
    local.frame = makeFrame(std::move(previous));
    anariRenderFrame(m_device, m_frame);
    anariFrameReady(m_device, m_frame, ANARI_WAIT);
    if (compositing) {
      local.depth =
          mapWrapped("channel.depth", ANARI_FLOAT32, local.frame.size);
      if (local.frame.colorType != ANARI_UNKNOWN) {
        local.color = mapWrapped(
            "channel.color", local.frame.colorType, local.frame.size);
      }
      stageIds(local.frame, (const float *)local.depth.data());
    }
  } catch (...) {
    if (!compositing)
      throw;
    local.failure = std::current_exception();
  }
  return local;
}

void FrameWrapper::compositeCollective(LocalRender &local)
{
  // Before the first collective, so that if any rank failed, all of them
  // skip compositing instead of the others hanging in it.
  if (agree(m_compositor->comm(), !local.failure)) {
    // reads the wrapped frame's channels in place; without channel.color it
    // composites transparent black, so depth still composites. If this
    // throws, 'local' unmaps the channels on the way out.
    RenderedFrame &frame = local.frame;
    m_compositor->run(frame.color.data(),
        frame.depth.data(),
        local.color.data() ? local.color.data() : frame.noColor.data(),
        (const float *)local.depth.data());
    m_idCompositor.composite(m_compositor->comm(), frame.ids);
    return; // 'local' unmaps the channels as its caller is done with it
  }
  local.depth.reset();
  local.color.reset();
  // nothing was composited, so there is nothing to map
  m_idCompositor.reset();
  if (local.failure)
    std::rethrow_exception(local.failure);
  throw std::runtime_error(
      "another rank failed to render or prepare "
      "this frame for compositing, so no rank "
      "composited it");
}

void FrameWrapper::completeFrame()
{
  // The composited frame is ready (rendering is synchronous), so the
  // continuation has run before any anariFrameReady() returns.
  auto callback = m_committed.completionCallback;
  if (!callback)
    return;
  // the app may release its last reference to the frame in there
  helium::IntrusivePtr<FrameWrapper> self(this);
  ScopedFlag inCallback(m_inCompletionCallback);
  callback(m_committed.completionCallbackUserData,
      m_ptcDevice->this_device(),
      (ANARIFrame)this);
}

void FrameWrapper::refuseFromCallback(const char *what) const
{
  if (m_inCompletionCallback) {
    throw StatusError(ANARI_STATUS_INVALID_OPERATION,
        std::string("cannot ") + what
            + " a frame from inside its own frame completion callback");
  }
}

FrameWrapper::MappedChannel FrameWrapper::mapWrapped(
    const char *channel, ANARIDataType type, anari::math::uint2 size)
{
  uint32_t width = 0, height = 0;
  ANARIDataType mappedType = ANARI_UNKNOWN;
  const void *data =
      anariMapFrame(m_device, m_frame, channel, &width, &height, &mappedType);
  if (data && mappedType == type && width == size.x && height == size.y)
    return MappedChannel(m_device, m_frame, channel, data);
  if (data)
    anariUnmapFrame(m_device, m_frame, channel);
  throw std::runtime_error(std::string("the wrapped frame did not render '")
      + channel + "' as " + anari::toString(type) + " of the frame's size");
}

FrameWrapper::MappedChannel::MappedChannel(
    ANARIDevice d, ANARIFrame f, const char *channel, const void *data)
    : m_device(d), m_frame(f), m_channel(channel), m_data(data)
{}

FrameWrapper::MappedChannel::MappedChannel(MappedChannel &&o) noexcept
    : m_device(o.m_device),
      m_frame(o.m_frame),
      m_channel(o.m_channel),
      m_data(std::exchange(o.m_data, nullptr))
{}

FrameWrapper::MappedChannel &FrameWrapper::MappedChannel::operator=(
    MappedChannel &&o) noexcept
{
  if (this != &o) {
    reset();
    m_device = o.m_device;
    m_frame = o.m_frame;
    m_channel = o.m_channel;
    m_data = std::exchange(o.m_data, nullptr);
  }
  return *this;
}

void FrameWrapper::MappedChannel::reset()
{
  if (m_data)
    anariUnmapFrame(m_device, m_frame, m_channel);
  m_data = nullptr;
}

void FrameWrapper::stageIds(const RenderedFrame &frame, const float *depth)
{
  const size_t numPixels = size_t(frame.size.x) * frame.size.y;
  m_idCompositor.stage(m_compositor->rank(),
      frame.size,
      depth,
      frame.idEnabled,
      [&](size_t channel, uint32_t *dst, size_t stride) {
        const char *name = ID_CHANNELS[channel];
        uint32_t width = 0, height = 0;
        ANARIDataType type = ANARI_UNKNOWN;
        auto *ids = (const uint32_t *)anariMapFrame(
            m_device, m_frame, name, &width, &height, &type);
        // still take part in the reduction, with no hits
        const bool valid =
            ids && type == ANARI_UINT32 && size_t(width) * height == numPixels;
        if (!valid) {
          const std::string msg =
              std::string("the wrapped frame did not render '") + name
              + "' as ANARI_UINT32; compositing it as no hits";
          reportStatus(ANARI_SEVERITY_WARNING, msg);
        }
        for (size_t p = 0; p < numPixels; p++)
          dst[p * stride] = valid ? ids[p] : IdCompositor::NO_HIT;
        anariUnmapFrame(m_device, m_frame, name);
      });
}

} // namespace anari_ptc
