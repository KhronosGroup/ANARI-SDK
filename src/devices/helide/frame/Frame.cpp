// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Frame.h"
// std
#include <algorithm>
#include <chrono>
// embree
#include "algorithms/parallel_for.h"

namespace helide {

// Frame definitions //////////////////////////////////////////////////////////

Frame::Frame(HelideGlobalState *s) : helium::BaseFrame(s) {}

Frame::~Frame()
{
  wait();
}

bool Frame::isValid() const
{
  return m_renderer && m_renderer->isValid() && m_camera && m_camera->isValid()
      && m_world && m_world->isValid() && m_frameData.size.x > 0
      && m_frameData.size.y > 0;
}

HelideGlobalState *Frame::deviceState() const
{
  return (HelideGlobalState *)helium::BaseObject::m_state;
}

void Frame::commitParameters()
{
  waitOnOutstandingWorkIfNeeded();

  m_renderer = getParamObject<Renderer>("renderer");
  m_camera = getParamObject<Camera>("camera");
  m_world = getParamObject<World>("world");
  m_incomingTypes.color =
      getParam<anari::DataType>("channel.color", ANARI_UNKNOWN);
  m_incomingTypes.depth =
      getParam<anari::DataType>("channel.depth", ANARI_UNKNOWN);
  m_incomingTypes.primId =
      getParam<anari::DataType>("channel.primitiveId", ANARI_UNKNOWN);
  m_incomingTypes.objId =
      getParam<anari::DataType>("channel.objectId", ANARI_UNKNOWN);
  m_incomingTypes.instId =
      getParam<anari::DataType>("channel.instanceId", ANARI_UNKNOWN);
  m_incomingFrameSize = getParam<uint2>("size", uint2(0u));
  m_callback = getParam<ANARIFrameCompletionCallback>(
      "frameCompletionCallback", nullptr);
  m_callbackUserPtr =
      getParam<void *>("frameCompletionCallbackUserData", nullptr);
}

void Frame::finalize()
{
  waitOnOutstandingWorkIfNeeded();

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

  if (m_incomingFrameSize.x == 0 || m_incomingFrameSize.y == 0)
    reportMessage(ANARI_SEVERITY_WARNING, "invalid frame dimensions");

  m_frameData.size = m_incomingFrameSize;
  m_currentTypes = m_incomingTypes;
  m_perPixelBytes = 4 * (m_currentTypes.color == ANARI_FLOAT32_VEC4 ? 4 : 1);

  if (m_frameData.size.x > 0 && m_frameData.size.y > 0)
    m_frameData.invSize = 1.f / float2(m_frameData.size);
  else
    m_frameData.invSize = float2(0.f);

  m_pixelBuffer.clear();
  m_depthBuffer.clear();
  m_primIdBuffer.clear();
  m_objIdBuffer.clear();
  m_instIdBuffer.clear();

  const auto numPixels = m_frameData.size.x * m_frameData.size.y;
  m_pixelBuffer.resize(numPixels * m_perPixelBytes);
  if (m_currentTypes.depth == ANARI_FLOAT32)
    m_depthBuffer.resize(numPixels);
  if (m_currentTypes.primId == ANARI_UINT32)
    m_primIdBuffer.resize(numPixels);
  if (m_currentTypes.objId == ANARI_UINT32)
    m_objIdBuffer.resize(numPixels);
  if (m_currentTypes.instId == ANARI_UINT32)
    m_instIdBuffer.resize(numPixels);

  m_frameChanged = true;
}

bool Frame::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t size,
    uint32_t flags)
{
  if (type == ANARI_FLOAT32 && name == "duration") {
    helium::writeToVoidP(ptr, m_duration);
    return true;
  }

  return 0;
}

void Frame::renderFrame()
{
  auto *state = deviceState();
  wait();

  this->refInc(helium::RefType::INTERNAL);

  state->taskQueue.enqueue([state]() { state->commitBuffer.flush(); });

  m_future = state->taskQueue.enqueue([this, state]() {
    auto start = std::chrono::steady_clock::now();
    state->renderingSemaphore.frameStart();

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
    const float frameAspect = float(size.x) / float(size.y);
    const auto rayGen = m_camera->createRayGenerator(frameAspect);
    using Range = embree::range<uint32_t>;
    embree::parallel_for(0u, size.y, taskGrainSize.y, [&](const Range &ry) {
      for (auto y = ry.begin(); y < ry.end(); y++) {
        embree::parallel_for(0u, size.x, taskGrainSize.x, [&](const Range &rx) {
          for (auto x = rx.begin(); x < rx.end(); x++) {
            auto screen = screenFromPixel(float2(x, y));
            auto imageRegion = m_camera->imageRegion();
            screen.x = linalg::lerp(imageRegion.x, imageRegion.z, screen.x);
            screen.y = linalg::lerp(imageRegion.y, imageRegion.w, screen.y);
            Ray ray = rayGen.createRay(screen);
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
  });
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
    *pixelType = m_currentTypes.color;
    return m_pixelBuffer.data();
  } else if (channel == "channel.depth" && !m_depthBuffer.empty()) {
    *pixelType = m_currentTypes.depth;
    return m_depthBuffer.data();
  } else if (channel == "channel.primitiveId" && !m_primIdBuffer.empty()) {
    *pixelType = m_currentTypes.primId;
    return m_primIdBuffer.data();
  } else if (channel == "channel.objectId" && !m_objIdBuffer.empty()) {
    *pixelType = m_currentTypes.objId;
    return m_objIdBuffer.data();
  } else if (channel == "channel.instanceId" && !m_instIdBuffer.empty()) {
    *pixelType = m_currentTypes.instId;
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
  return helium::tasking::isReady(m_future);
}

void Frame::wait()
{
  if (m_future.valid()) {
    m_future.get();
    this->refDec(helium::RefType::INTERNAL);
  }
}

void Frame::waitOnOutstandingWorkIfNeeded()
{
  auto *state = deviceState();
  if (!state->taskQueue.onWorkerThread())
    wait();
}

float2 Frame::screenFromPixel(const float2 &p) const
{
  return p * m_frameData.invSize;
}

void Frame::writeSample(int x, int y, const PixelSample &s)
{
  const auto idx = y * m_frameData.size.x + x;
  auto *color = m_pixelBuffer.data() + (idx * m_perPixelBytes);
  switch (m_currentTypes.color) {
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
