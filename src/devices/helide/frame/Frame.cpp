// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Frame.h"
// std
#include <algorithm>
#include <chrono>
#include <random>
// embree
#include "algorithms/parallel_for.h"

namespace helide {

// Helper functions ///////////////////////////////////////////////////////////

template <typename I, typename FUNC>
static void serial_for(I size, FUNC &&f)
{
  for (I i = 0; i < size; i++)
    f(i);
}

template <typename R, typename TASK_T>
static std::future<R> async(std::packaged_task<R()> &task, TASK_T &&fcn)
{
#if 1
  return std::async(fcn);
#else
  task = std::packaged_task<R()>(std::forward<TASK_T>(fcn));
  auto future = task.get_future();
  embree::TaskScheduler::spawn([&]() { task(); });
  return future;
#endif
}

template <typename R>
static bool is_ready(const std::future<R> &f)
{
  return !f.valid()
      || f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

// Frame definitions //////////////////////////////////////////////////////////

Frame::Frame(HelideGlobalState *s) : helium::BaseFrame(s) {}

Frame::~Frame()
{
  wait();
}

bool Frame::isValid() const
{
  return m_valid;
}

HelideGlobalState *Frame::deviceState() const
{
  return (HelideGlobalState *)helium::BaseObject::m_state;
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
  m_frameData.size = getParam<uint2>("size", uint2(10));
  m_callback = getParam<ANARIFrameCompletionCallback>(
      "frameCompletionCallback", nullptr);
  m_callbackUserPtr =
      getParam<void *>("frameCompletionCallbackUserData", nullptr);
}

void Frame::finalize()
{
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

  m_valid = m_renderer && m_renderer->isValid() && m_camera
      && m_camera->isValid() && m_world && m_world->isValid();

  m_frameData.invSize = 1.f / float2(m_frameData.size);

  const auto numPixels = m_frameData.size.x * m_frameData.size.y;

  m_perPixelBytes = 4 * (m_colorType == ANARI_FLOAT32_VEC4 ? 4 : 1);
  m_pixelBuffer.resize(numPixels * m_perPixelBytes);

  m_depthBuffer.resize(m_depthType == ANARI_FLOAT32 ? numPixels : 0);
  m_frameChanged = true;

  m_primIdBuffer.clear();
  m_objIdBuffer.clear();
  m_instIdBuffer.clear();

  if (m_primIdType == ANARI_UINT32)
    m_primIdBuffer.resize(numPixels);
  if (m_objIdType == ANARI_UINT32)
    m_objIdBuffer.resize(numPixels);
  if (m_instIdType == ANARI_UINT32)
    m_instIdBuffer.resize(numPixels);
}

bool Frame::getProperty(
    const std::string_view &name, ANARIDataType type, void *ptr, uint64_t size, uint32_t flags)
{
  if (type == ANARI_FLOAT32 && name == "duration") {
    helium::writeToVoidP(ptr, m_duration);
    return true;
  }

  return 0;
}

void Frame::renderFrame()
{
  this->refInc(helium::RefType::INTERNAL);

  auto *state = deviceState();
  state->waitOnCurrentFrame();
  state->currentFrame = this;

  auto doRender = [&, state]() {
    auto start = std::chrono::steady_clock::now();
    state->renderingSemaphore.frameStart();
    state->commitBuffer.flush();

    if (!isValid()) {
      reportMessage(
          ANARI_SEVERITY_ERROR, "skipping render of incomplete frame object");
      std::fill(m_pixelBuffer.begin(), m_pixelBuffer.end(), 0);
      state->renderingSemaphore.frameEnd();
      return;
    }

    if (state->commitBuffer.lastObjectFinalization() <= m_frameLastRendered) {
      state->renderingSemaphore.frameEnd();
      return;
    }

    m_frameLastRendered = helium::newTimeStamp();

    // NOTE(jda) - We don't want any anariGetProperty() calls also trying to
    //             rebuild the Embree scene in parallel to us doing a rebuild.
    auto worldLock = m_world->scopeLockObject();
    m_world->embreeSceneUpdate();

    const auto taskGrainSize = uint2(m_renderer->taskGrainSize());

    const auto &size = m_frameData.size;
    using Range = embree::range<uint32_t>;
    embree::parallel_for(0u, size.y, taskGrainSize.y, [&](const Range &ry) {
      for (auto y = ry.begin(); y < ry.end(); y++) {
        embree::parallel_for(0u, size.x, taskGrainSize.x, [&](const Range &rx) {
          for (auto x = rx.begin(); x < rx.end(); x++) {
            auto screen = screenFromPixel(float2(x, y));
            auto imageRegion = m_camera->imageRegion();
            screen.x = linalg::lerp(imageRegion.x, imageRegion.z, screen.x);
            screen.y = linalg::lerp(imageRegion.y, imageRegion.w, screen.y);
            Ray ray = m_camera->createRay(screen);
            writeSample(x, y, m_renderer->renderSample(screen, ray, *m_world));
          }
        });
      }
    });

    if (m_callback)
      m_callback(m_callbackUserPtr, state->anariDevice, (ANARIFrame)this);

    state->renderingSemaphore.frameEnd();

    auto end = std::chrono::steady_clock::now();
    m_duration = std::chrono::duration<float>(end - start).count();
  };

  m_future = async<void>(m_task, doRender);
}

void *Frame::map(std::string_view channel,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *pixelType)
{
  wait();

  *width = m_frameData.size.x;
  *height = m_frameData.size.y;

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
  if (m == ANARI_NO_WAIT)
    return ready();
  else {
    wait();
    return 1;
  }
}

void Frame::discard()
{
  // no-op
}

bool Frame::ready() const
{
  return is_ready(m_future);
}

void Frame::wait()
{
  if (m_future.valid()) {
    m_future.get();
    this->refDec(helium::RefType::INTERNAL);
    if (deviceState()->currentFrame == this)
      deviceState()->currentFrame = nullptr;
  }
}

float2 Frame::screenFromPixel(const float2 &p) const
{
  return p * m_frameData.invSize;
}

void Frame::writeSample(int x, int y, const PixelSample &s)
{
  const auto idx = y * m_frameData.size.x + x;
  auto *color = m_pixelBuffer.data() + (idx * m_perPixelBytes);
  switch (m_colorType) {
  case ANARI_UFIXED8_VEC4: {
    auto c = helium::math::cvt_color_to_uint32(s.color);
    std::memcpy(color, &c, sizeof(c));
    break;
  }
  case ANARI_UFIXED8_RGBA_SRGB: {
    auto c = helium::math::cvt_color_to_uint32_srgb(s.color);
    std::memcpy(color, &c, sizeof(c));
    break;
  }
  case ANARI_FLOAT32_VEC4: {
    std::memcpy(color, &s.color, sizeof(s.color));
    break;
  }
  default:
    break;
  }
  if (!m_depthBuffer.empty())
    m_depthBuffer[idx] = s.depth;
  if (!m_primIdBuffer.empty())
    m_primIdBuffer[idx] = s.primId;
  if (!m_objIdBuffer.empty())
    m_objIdBuffer[idx] = s.objId;
  if (!m_instIdBuffer.empty())
    m_instIdBuffer[idx] = s.instId;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Frame *);
