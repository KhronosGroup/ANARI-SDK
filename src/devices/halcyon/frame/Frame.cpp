// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Frame.h"
// std
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
// embree
#include "algorithms/parallel_for.h"

#include "Frame_ispc.h"

namespace halcyon {

// Frame definitions //////////////////////////////////////////////////////////

Frame::Frame(HalcyonDeviceGlobalState *s) : helium::BaseFrame(s)
{
  auto ispcStorage = s->registry.frames.emplace();
  m_ispcIndex = ispcStorage.index();
  m_ispcSlot = *ispcStorage;

  ispc::Frame_setBuffers(getIE(), nullptr, nullptr);
  ispc::Frame_setIdBuffers(getIE(), nullptr, nullptr, nullptr);
  ispc::Frame_setAlbedoAndNormalBuffers(
      getIE(), ANARI_UNKNOWN, nullptr, ANARI_UNKNOWN, nullptr);
  ispc::Frame_setColorFormat(getIE(), ANARI_UNKNOWN);
  ispc::Frame_setSize(getIE(), 0, 0);
  ispc::Frame_setCamera(getIE(), nullptr);
  ispc::Frame_setRenderer(getIE(), nullptr);

  if (ispc::sizeof_Frame() != ispcStorage->size()) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "sizeof(ispc::Frame) == %zu | sizeof(ispcStorage) == %zu",
        ispc::sizeof_Frame(),
        ispcStorage->size());
  }
}

Frame::~Frame()
{
  wait();
  auto &state = *deviceState();
  state.registry.frames.erase(m_ispcIndex);
}

bool Frame::isValid() const
{
  return m_renderer && m_renderer->isValid() && m_camera && m_camera->isValid()
      && m_world && m_world->isValid() && m_size.x > 0 && m_size.y > 0;
}

HalcyonDeviceGlobalState *Frame::deviceState() const
{
  return (HalcyonDeviceGlobalState *)helium::BaseObject::m_state;
}

void Frame::commitParameters()
{
  m_renderer = getParamObject<Renderer>("renderer");
  m_camera = getParamObject<Camera>("camera");
  m_world = getParamObject<World>("world");
  m_colorType = getParam<anari::DataType>("channel.color", ANARI_UNKNOWN);
  m_depthType = getParam<anari::DataType>("channel.depth", ANARI_UNKNOWN);
  m_primIdType =
      getParam<anari::DataType>("channel.primitiveId", ANARI_UNKNOWN);
  m_objIdType = getParam<anari::DataType>("channel.objectId", ANARI_UNKNOWN);
  m_instIdType = getParam<anari::DataType>("channel.instanceId", ANARI_UNKNOWN);
  // KHR_FRAME_CHANNEL_ALBEDO/_NORMAL: other pixel types leave the channel
  // unmapped, as for the other channels
  m_albedoType = getParam<anari::DataType>("channel.albedo", ANARI_UNKNOWN);
  if (m_albedoType != ANARI_FLOAT32_VEC3 && m_albedoType != ANARI_UFIXED8_VEC3
      && m_albedoType != ANARI_UFIXED8_RGB_SRGB)
    m_albedoType = ANARI_UNKNOWN;
  m_normalType = getParam<anari::DataType>("channel.normal", ANARI_UNKNOWN);
  if (m_normalType != ANARI_FLOAT32_VEC3 && m_normalType != ANARI_FIXED16_VEC3)
    m_normalType = ANARI_UNKNOWN;
  m_size = getParam<uint2>("size", uint2(0u, 0u));
  // 'accumulation' isn't read: renderers that refine their image always do
  // (Renderer::wantsRender())
  m_callback = getParam<ANARIFrameCompletionCallback>(
      "frameCompletionCallback", nullptr);
  m_callbackUserPtr =
      getParam<void *>("frameCompletionCallbackUserData", nullptr);
}

void Frame::finalize()
{
  // Every commit buffer flush runs on the task queue (renders, object
  // ANARI_WAIT queries), after the renders queued before it, so no render of
  // this frame is in flight here
  assert(deviceState()->taskQueue.onWorkerThread());

  if (!m_renderer) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'renderer' on frame");
  }

  if (!m_camera) {
    reportMessage(
        ANARI_SEVERITY_WARNING, "missing required parameter 'camera' on frame");
  }

  if (!m_world) {
    reportMessage(
        ANARI_SEVERITY_WARNING, "missing required parameter 'world' on frame");
  }

  if (m_size.x == 0 || m_size.y == 0)
    reportMessage(ANARI_SEVERITY_WARNING, "invalid frame dimensions");

  const auto numPixels = size_t(m_size.x) * m_size.y;

  m_perPixelBytes = 4 * (m_colorType == ANARI_FLOAT32_VEC4 ? 4 : 1);
  m_pixelBuffer.resize(numPixels * m_perPixelBytes);
  m_depthBuffer.resize(m_depthType == ANARI_FLOAT32 ? numPixels : 0);
  m_primIdBuffer.resize(m_primIdType == ANARI_UINT32 ? numPixels : 0);
  m_objIdBuffer.resize(m_objIdType == ANARI_UINT32 ? numPixels : 0);
  m_instIdBuffer.resize(m_instIdType == ANARI_UINT32 ? numPixels : 0);
  m_albedoBuffer.resize(m_albedoType == ANARI_UNKNOWN
          ? 0
          : numPixels * (m_albedoType == ANARI_FLOAT32_VEC3 ? 12 : 3));
  m_normalBuffer.resize(m_normalType == ANARI_UNKNOWN
          ? 0
          : numPixels * (m_normalType == ANARI_FLOAT32_VEC3 ? 12 : 6));
  const auto ptr = [](auto &buffer) -> void * {
    return buffer.empty() ? nullptr : buffer.data();
  };
  ispc::Frame_setAlbedoAndNormalBuffers(getIE(),
      m_albedoType,
      ptr(m_albedoBuffer),
      m_normalType,
      ptr(m_normalBuffer));

  ispc::Frame_setBuffers(getIE(),
      m_pixelBuffer.data(),
      m_depthBuffer.empty() ? nullptr : m_depthBuffer.data());
  ispc::Frame_setIdBuffers(getIE(),
      m_primIdBuffer.empty() ? nullptr : m_primIdBuffer.data(),
      m_objIdBuffer.empty() ? nullptr : m_objIdBuffer.data(),
      m_instIdBuffer.empty() ? nullptr : m_instIdBuffer.data());
  ispc::Frame_setColorFormat(getIE(), m_colorType);
  ispc::Frame_setSize(getIE(), int(m_size.x), int(m_size.y));
  ispc::Frame_setCamera(getIE(), m_camera ? m_camera->getIE() : nullptr);
  ispc::Frame_setRenderer(getIE(), m_renderer ? m_renderer->getIE() : nullptr);
  ispc::Frame_setWorld(getIE(), m_world ? m_world->getIE() : nullptr);
}

bool Frame::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t size,
    uint32_t flags)
{
  if (type == ANARI_FLOAT32 && name == "duration") {
    helium::writeToVoidP(ptr, m_duration.load());
    return true;
  }

  // the renderer's, as of the frame's last render (an ANARI_WAIT query runs
  // after the renders queued before it)
  std::lock_guard<std::mutex> lock(m_rendererStateMutex);
  if (m_rendererState)
    return m_rendererState->getProperty(name, type, ptr, size, flags);
  return 0;
}

void Frame::renderFrame()
{
  // m_future tracks the job running the callback, and an app thread may be
  // blocked on it; replacing it from here would race with that wait.
  if (completingOnThisThread()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "halcyon does not support rendering a frame from its own completion "
        "callback");
    return;
  }

  auto *state = deviceState();
  if (!wait()) {
    // Dropped: waiting on the queued render would deadlock (wait() reported
    // the error). The completion callback still fires once for this request,
    // from a job queued behind that render. The render's internal reference
    // and the duty to drop it in wait() pass to the new future: the single
    // worker runs jobs in order, so it completes after the render's.
    // Its duration stays that of the last render.
    m_jobsAwaitingDeviceWork++;
    m_future = state->taskQueue.enqueue([this, state]() {
      m_jobsAwaitingDeviceWork--;
      invokeCompletionCallback(
          m_callback, m_callbackUserPtr, state->anariDevice);
    });
    return;
  }

  this->refInc(helium::RefType::INTERNAL);
  m_jobsAwaitingDeviceWork++;

  m_future = state->taskQueue.enqueue([this, state]() {
    auto start = std::chrono::steady_clock::now();

    // Commits copy array data and renders read it, so both wait for any
    // mapped arrays to be unmapped, and mapping waits for them
    {
      RenderingSemaphore::DeviceWork deviceWork(state->renderingSemaphore);
      flushAndRender();
      // before the device work ends, so a thread that maps an array after it
      // sees this render as done
      m_jobsAwaitingDeviceWork--;
    }

    // Unlike helide, every render fires the completion callback, including
    // one skipped because nothing changed or refused as invalid, so apps
    // expecting one callback per render don't stall; such a job's duration
    // is the (short) time it took.
    //
    // Also unlike helide, the render's device work has ended before the
    // callback runs, so the callback may map arrays. The duration includes the
    // callback.
    invokeCompletionCallback(m_callback, m_callbackUserPtr, state->anariDevice);
    auto end = std::chrono::steady_clock::now();
    m_duration = std::chrono::duration<float>(end - start).count();
  });
}

void Frame::flushAndRender()
{
  auto *state = deviceState();

  state->applyDeviceParameters();
  state->flushCommitBuffer();

  // only the color channel is cleared; depth and ids keep the last render's
  // values, like helide
  if (!isValid()) {
    reportMessage(
        ANARI_SEVERITY_ERROR, "skipping render of incomplete frame object");
    std::fill(m_pixelBuffer.begin(), m_pixelBuffer.end(), 0);
    // the renderer's state goes too, so the frame reports none of its
    // properties
    setRendererState(nullptr, nullptr);
    return;
  }

  // A renderer's state for this frame, made the first time the frame
  // renders with it; a new state counts as a change
  bool newState = false;
  if (m_rendererStateOwner != m_renderer) {
    setRendererState(m_renderer->newFrameState(), m_renderer);
    newState = true;
  }

  // Nothing changed since the last render (no object finalized and no device
  // parameters committed): skip the render, unless the renderer wants to go
  // on (a renderer that refines its image, until it is done)
  const auto lastChange = std::max(state->commitBuffer.lastObjectFinalization(),
      state->lastDeviceParametersApplied);
  const bool changed = lastChange > m_frameLastRendered || newState;
  if (!m_renderer->wantsRender(m_rendererState.get(), changed))
    return;

  m_frameLastRendered = helium::newTimeStamp();
  m_world->embreeSceneUpdate();
  // a device parameter, which can change between renders
  ispc::Frame_setInvalidMaterialColor(getIE(), &state->invalidMaterialColor.x);

  m_renderer->render(*this, m_rendererState.get(), changed);
}

void Frame::setRendererState(std::unique_ptr<RendererFrameState> state,
    helium::IntrusivePtr<Renderer> owner)
{
  std::lock_guard<std::mutex> lock(m_rendererStateMutex);
  m_rendererState = std::move(state);
  m_rendererStateOwner = std::move(owner);
}

void Frame::runPass(const std::function<void(int x, int y)> &spanFcn)
{
  // Tasks cover whole SIMD-wide pixel spans along x, so the renderer's
  // width grain size is rounded up to a multiple of the ISPC program count
  const int programCount = ispc::get_programCount();
  const int numSpans = (int(m_size.x) + programCount - 1) / programCount;
  const int2 grain = m_renderer->taskGrainSize();
  const int spanGrain = (grain.x + programCount - 1) / programCount;

  // Each task covers one tile of grain.y rows by spanGrain spans
  const int numTilesX = (numSpans + spanGrain - 1) / spanGrain;
  const int numTilesY = (int(m_size.y) + grain.y - 1) / grain.y;
  embree::parallel_for(numTilesX * numTilesY, [&](int tile) {
    const int s0 = (tile % numTilesX) * spanGrain;
    const int y0 = (tile / numTilesX) * grain.y;
    const int s1 = std::min(s0 + spanGrain, numSpans);
    const int y1 = std::min(y0 + grain.y, int(m_size.y));
    for (int y = y0; y < y1; y++) {
      for (int s = s0; s < s1; s++)
        spanFcn(s * programCount, y);
    }
  });
}

void *Frame::map(std::string_view channel,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *pixelType)
{
  // from another frame's completion callback, or from a thread with an array
  // mapped, reports an error and maps the current buffers, which a queued
  // render will overwrite
  wait();

  *width = m_size.x;
  *height = m_size.y;

  if (channel == "channel.color") {
    *pixelType = m_colorType;
    return m_pixelBuffer.data();
  } else if (channel == "channel.depth" && !m_depthBuffer.empty()) {
    *pixelType = ANARI_FLOAT32;
    return m_depthBuffer.data();
  } else if (channel == "channel.primitiveId" && !m_primIdBuffer.empty()) {
    *pixelType = ANARI_UINT32;
    return m_primIdBuffer.data();
  } else if (channel == "channel.objectId" && !m_objIdBuffer.empty()) {
    *pixelType = ANARI_UINT32;
    return m_objIdBuffer.data();
  } else if (channel == "channel.instanceId" && !m_instIdBuffer.empty()) {
    *pixelType = ANARI_UINT32;
    return m_instIdBuffer.data();
  } else if (channel == "channel.albedo" && !m_albedoBuffer.empty()) {
    *pixelType = m_albedoType;
    return m_albedoBuffer.data();
  } else if (channel == "channel.normal" && !m_normalBuffer.empty()) {
    *pixelType = m_normalType;
    return m_normalBuffer.data();
  } else {
    *width = 0;
    *height = 0;
    *pixelType = ANARI_UNKNOWN;
    return nullptr;
  }
}

void Frame::unmap(std::string_view channel)
{
  // no-op
}

int Frame::frameReady(ANARIWaitMask m)
{
  if (m == ANARI_NO_WAIT && !ready())
    return 0;
  // Waiting on a finished render doesn't block, and drops its reference to
  // this frame: a frame released after it was polled, or after it finished
  // without being waited on (see helium::BaseFrame::on_NoPublicReferences()),
  // is destroyed instead of leaked. That may delete this frame.
  return wait() ? 1 : 0;
}

void Frame::discard()
{
  // no-op
}

bool Frame::ready() const
{
  return completingOnThisThread() || helium::tasking::isReady(m_future);
}

bool Frame::wait()
{
  // A completion callback runs inside the job m_future tracks; that job has
  // finished rendering, and waiting on it from there would never return.
  if (completingOnThisThread())
    return true;
  if (!m_future.valid())
    return true;
  // Elsewhere on the worker (another frame's completion callback), a job not
  // yet finished is queued behind the running one
  if (deviceState()->taskQueue.onWorkerThread()
      && !helium::tasking::isReady(m_future)) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "waiting on a frame from another frame's completion callback would "
        "deadlock; not waiting");
    return false;
  }
  // Device work this frame's jobs wait for can't start while this thread
  // holds it off
  if (m_jobsAwaitingDeviceWork > 0) {
    if (const char *why =
            deviceState()->renderingSemaphore.whyThisThreadCantWait()) {
      reportMessage(ANARI_SEVERITY_ERROR,
          "waiting on a frame would deadlock: %s; not waiting",
          why);
      return false;
    }
  }
  m_future.get();
  // may delete this frame
  this->refDec(helium::RefType::INTERNAL);
  return true;
}

void *Frame::getIE()
{
  return m_ispcSlot;
}

uint2 Frame::size() const
{
  return m_size;
}

const World &Frame::world() const
{
  return *m_world;
}

bool Frame::hasAlbedoChannel() const
{
  return !m_albedoBuffer.empty();
}

bool Frame::hasNormalChannel() const
{
  return !m_normalBuffer.empty();
}

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_DEFINITION(halcyon::Frame *);
