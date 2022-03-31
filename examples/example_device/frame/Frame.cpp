// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Frame.h"
// std
#include <algorithm>
#include <random>

namespace anari {
namespace example_device {

// Helper functions ///////////////////////////////////////////////////////////

static uint32_t cvt_uint32(const float &f)
{
  return (uint32_t)(255.f * glm::clamp(f, 0.f, 1.f));
}

static uint32_t cvt_uint32(const vec4 &v)
{
  return (cvt_uint32(v.x) << 0) | (cvt_uint32(v.y) << 8)
      | (cvt_uint32(v.z) << 16) | (cvt_uint32(v.w) << 24);
}

static uint32_t cvt_uint32_srgb(const vec4 &v)
{
  return cvt_uint32(vec4(
      pow(v.x, 1.f / 2.2f), pow(v.y, 1.f / 2.2f), pow(v.z, 1.f / 2.2f), v.w));
}

// Frame definitions //////////////////////////////////////////////////////////

Frame::Frame()
{
  setCommitPriority(COMMIT_PRIORITY_FRAME);
}

Frame::~Frame()
{
  if (futureIsValid())
    m_future->wait();
}

void Frame::commit()
{
  m_renderer = getParamObject<Renderer>("renderer");
  m_camera = getParamObject<Camera>("camera");
  m_world = getParamObject<World>("world");

  if (!m_renderer)
    throw std::runtime_error("missing 'renderer' on frame");
  if (!m_camera)
    throw std::runtime_error("missing 'camera' on frame");
  if (!m_world)
    throw std::runtime_error("missing 'world' on frame");

  m_format = getParam<ANARIDataType>("color", ANARI_UFIXED8_RGBA_SRGB);
  m_size = getParam<uvec2>("size", ivec2(10));
  m_invSize = 1.f / vec2(m_size);

  m_continuation = getParam<ANARIFrameCompletionCallback>(
      "frameCompletionCallback", nullptr);
  m_continuationPtr =
      getParam<void *>("frameCompletionCallbackUserData", nullptr);

  auto numPixels = m_size.x * m_size.y;

  m_accum.resize(numPixels);
  m_mappedPixelBuffer.resize(numPixels * sizeOf(m_format));
  m_depthBuffer.resize(numPixels);

  m_frameChanged = true;
}

ivec2 Frame::size() const
{
  return m_size;
}

ANARIDataType Frame::format() const
{
  return m_format;
}

bool Frame::getProperty(
    const std::string &name, ANARIDataType type, void *ptr, ANARIWaitMask mask)
{
  if (name == "duration" && type == ANARI_FLOAT32 && futureIsValid()) {
    if (mask & ANARI_WAIT)
      m_future->wait();
    float d = duration();
    std::memcpy(ptr, &d, sizeof(d));
    return true;
  }

  return Object::getProperty(name, type, ptr, mask);
}

void *Frame::mapColorBuffer()
{
  switch (format()) {
  case ANARI_UFIXED8_VEC4: {
    std::transform(m_accum.begin(),
        m_accum.end(),
        (uint32_t *)m_mappedPixelBuffer.data(),
        [&](const vec4 &in) { return cvt_uint32(in * m_invFrameID); });
    break;
  }
  case ANARI_UFIXED8_RGBA_SRGB: {
    std::transform(m_accum.begin(),
        m_accum.end(),
        (uint32_t *)m_mappedPixelBuffer.data(),
        [&](const vec4 &in) { return cvt_uint32_srgb(in * m_invFrameID); });
    break;
  }
  case ANARI_FLOAT32_VEC4: {
    std::transform(m_accum.begin(),
        m_accum.end(),
        (vec4 *)m_mappedPixelBuffer.data(),
        [&](const vec4 &in) { return in * m_invFrameID; });
    break;
  }
  default: {
    throw std::runtime_error("cannot map ANARI_UNKNOWN format");
  }
  }

  return m_mappedPixelBuffer.data();
}

float *Frame::mapDepthBuffer()
{
  return m_depthBuffer.data();
}

void Frame::renderFrame(int threads)
{
  Renderer &r = *m_renderer;
  Camera &c = *m_camera;
  World &w = *m_world;

  for (int i = 0; i < r.pixelSamples(); i++) {
    newFrame();

#pragma omp parallel for schedule(dynamic, 64) collapse(2) num_threads(threads)
    for (int y = 0; y < size().y; ++y) {
      for (int x = 0; x < size().x; ++x) {
        const vec2 rdraw(randUniformDist() - 0.5f, randUniformDist() - 0.5f);
        const vec2 pixel(x + rdraw.x, y + rdraw.y);

        auto screen = screenFromPixel(pixel);

        Ray ray = c.createRay(screen);
        ray.t.lower = 0.f;
        ray.t.upper = std::numeric_limits<float>::max();

        const auto sample = r.renderSample(ray, w);

        auto &accum = m_accum[y * size().x + x];
        auto &depth = m_depthBuffer[y * size().x + x];

        accum = m_frameID == 0 ? sample.color : accum + sample.color;
        depth = m_frameID == 0 ? sample.depth : std::min(sample.depth, depth);
      }
    }
  }
}

void Frame::invokeContinuation(ANARIDevice device) const
{
  if (m_continuation)
    m_continuation(m_continuationPtr, device, (ANARIFrame)this);
}

void Frame::renewFuture()
{
  m_future = std::make_shared<Future>();
}

bool Frame::futureIsValid() const
{
  return m_future.get() != nullptr;
}

FuturePtr Frame::future()
{
  return m_future;
}

void Frame::setDuration(float d)
{
  m_lastFrameDuration = d;
}

float Frame::duration() const
{
  return m_lastFrameDuration;
}

int Frame::frameID() const
{
  return m_frameID;
}

void Frame::newFrame()
{
  bool cameraChanged = m_cameraLastChanged < m_camera->lastUpdated();
  if (cameraChanged)
    m_cameraLastChanged = m_camera->lastUpdated();

  bool rendererChanged = m_rendererLastChanged < m_renderer->lastUpdated();
  if (rendererChanged)
    m_rendererLastChanged = m_renderer->lastUpdated();

  if (cameraChanged || rendererChanged || m_frameChanged)
    m_frameID = 0;
  else
    m_frameID++;

  m_invFrameID = 1.f / (m_frameID + 1);
  m_frameChanged = false;
}

vec2 Frame::screenFromPixel(const vec2 &p) const
{
  return p * m_invSize;
}

} // namespace example_device

ANARI_TYPEFOR_DEFINITION(example_device::Frame *);

} // namespace anari
