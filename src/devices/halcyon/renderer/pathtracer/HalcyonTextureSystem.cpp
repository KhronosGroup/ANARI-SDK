// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "HalcyonTextureSystem.h"
#include "ColorManagement.h"
#include "HalcyonTextureSystem_ispc.h"
// typhoon
#include <renderer/materials/MaterialXCpp/nodeRegistry.h>
// std
#include <algorithm>
#include <charconv>
#include <cmath>
#include <mutex>
#include <set>

namespace halcyon::pathtracer {

const char *const SAMPLER_NODE_TYPE = "halcyon_sampler_vector4";

namespace {

const std::string SAMPLER_KEY_PREFIX = "anari:sampler/";

const mxcpp::SlotName SLOT_SAMPLER("sampler");
const mxcpp::SlotName SLOT_IN("in");
const mxcpp::SlotName SLOT_DEFAULT("default");
const mxcpp::SlotName SLOT_OUT("out");

// SAMPLER_NODE_TYPE's evaluation (see HalcyonTextureSystem.h)
void evalSamplerNode(const mxcpp::ParamMap &inputs,
    const mxcpp::ShadingContext &ctx,
    mxcpp::NodeOutputMap *outputs)
{
  mxcpp::Vec4f value = mxcpp::Get<mxcpp::Vec4f>(
      inputs, SLOT_DEFAULT, mxcpp::Vec4f(0.f, 0.f, 0.f, 1.f));
  const auto *textures =
      dynamic_cast<const HalcyonTextureSystem *>(ctx.textureSystem);
  if (textures) {
    textures->sample(mxcpp::Get<int>(inputs, SLOT_SAMPLER, -1),
        mxcpp::Get<mxcpp::Vec4f>(
            inputs, SLOT_IN, mxcpp::Vec4f(0.f, 0.f, 0.f, 1.f)),
        uint32_t(ctx.faceId),
        value);
  }
  (*outputs)[SLOT_OUT] = mxcpp::Value(value);
}

mxcpp::Texture2DResult defaultResult(
    const mxcpp::Texture2DRequest &request, mxcpp::TextureSampleStatus status)
{
  mxcpp::Texture2DResult result;
  result.value = request.defaultValue;
  result.status = status;
  return result;
}

mxcpp::Texture2DResult missing(const mxcpp::Texture2DRequest &request)
{
  return defaultResult(request, mxcpp::TextureSampleStatus::Missing);
}

// The wrap mode of address mode 'mode' inside [0, 1]; 'outside' tells
// whether coordinates outside it give the default instead
ispc::WrapMode wrapMode(mxcpp::TextureAddressMode mode, bool &defaultOutside)
{
  defaultOutside = false;
  switch (mode) {
  case mxcpp::TextureAddressMode::Clamp:
    return ispc::WRAP_MODE_CLAMP_TO_EDGE;
  case mxcpp::TextureAddressMode::Mirror:
    return ispc::WRAP_MODE_MIRROR_REPEAT;
  case mxcpp::TextureAddressMode::Constant:
  case mxcpp::TextureAddressMode::UseMetadata:
    // image files carry no wrap metadata here
    defaultOutside = true;
    return ispc::WRAP_MODE_CLAMP_TO_EDGE;
  case mxcpp::TextureAddressMode::Periodic:
  default:
    return ispc::WRAP_MODE_REPEAT;
  }
}

} // namespace

void registerTextureNodes()
{
  static std::once_flag once;
  std::call_once(once, [] {
    mxcpp::NodeRegistry::RegisterBuiltinNodes();
    mxcpp::NodeRegistry::GetInstance().Register(
        SAMPLER_NODE_TYPE, &evalSamplerNode);
  });
}

std::vector<std::string> imageFiles(const mxcpp::MaterialGraph &graph)
{
  std::set<std::string> files;
  for (const auto &entry : graph.nodes) {
    const mxcpp::GraphNode &node = entry.second;
    const auto it = node.parameters.find("file");
    if (it == node.parameters.end()
        || !mxcpp::ValueHolds<std::string>(it->second))
      continue;
    const std::string &file = mxcpp::ValueGet<std::string>(it->second);
    if (!file.empty() && file.rfind(SAMPLER_KEY_PREFIX, 0) != 0)
      files.insert(file);
  }
  return {files.begin(), files.end()};
}

int samplerInputAttribute(const void *sampler)
{
  const ispc::Attribute a = ispc::HalcyonTexture_inAttribute(sampler);
  return a == ispc::ATTRIBUTE_NONE ? -1 : int(a);
}

// HalcyonTextureSystem ///////////////////////////////////////////////////////

HalcyonTextureSystem::HalcyonTextureSystem(
    std::vector<const void *> samplers, ImageFiles images)
    : m_samplers(std::move(samplers)), m_images(std::move(images))
{}

std::string HalcyonTextureSystem::samplerKey(int index)
{
  return SAMPLER_KEY_PREFIX + std::to_string(index);
}

int HalcyonTextureSystem::samplerIndex(const std::string &key)
{
  if (key.compare(0, SAMPLER_KEY_PREFIX.size(), SAMPLER_KEY_PREFIX) != 0)
    return -1;
  const char *first = key.data() + SAMPLER_KEY_PREFIX.size();
  const char *last = key.data() + key.size();
  int index = -1;
  // digits only, all of them, without leading zeros: one key per sampler
  if (first == last || *first < '0' || *first > '9'
      || (*first == '0' && last - first > 1))
    return -1;
  const auto [end, error] = std::from_chars(first, last, index);
  return error == std::errc() && end == last ? index : -1;
}

bool HalcyonTextureSystem::sample(int index,
    const mxcpp::Vec4f &attribute,
    uint32_t primId,
    mxcpp::Vec4f &result) const
{
  if (index < 0 || size_t(index) >= m_samplers.size())
    return false;
  const float in[4] = {attribute[0], attribute[1], attribute[2], attribute[3]};
  float out[4];
  if (!ispc::HalcyonTexture_sample(m_samplers[index], in, primId, out))
    return false;
  result = mxcpp::Vec4f(out[0], out[1], out[2], out[3]);
  return true;
}

mxcpp::Texture2DResult HalcyonTextureSystem::Sample2D(
    const mxcpp::Texture2DRequest &request) const
{
  // Samplers that take no input at a texture coordinate (primitive samplers,
  // inAttribute "none") and invalid ones are Missing
  const int index = samplerIndex(request.filePath);
  if (index < 0 && request.filePath.rfind(SAMPLER_KEY_PREFIX, 0) != 0) {
    const auto image = m_images.find(request.filePath);
    if (image == m_images.end() || !image->second)
      return defaultResult(request, mxcpp::TextureSampleStatus::Error);
    return sampleFile(request, *image->second);
  }
  if (index < 0 || size_t(index) >= m_samplers.size()
      || samplerInputAttribute(m_samplers[index]) < 0)
    return missing(request);

  mxcpp::Texture2DResult result;
  if (!sample(index,
          mxcpp::Vec4f(request.st[0], request.st[1], 0.f, 1.f),
          0u,
          result.value))
    return missing(request);
  result.status = mxcpp::TextureSampleStatus::Ok;
  return result;
}

mxcpp::Texture2DResult HalcyonTextureSystem::sampleFile(
    const mxcpp::Texture2DRequest &request, const ImageFile &image) const
{
  bool outsideS = false, outsideT = false;
  const ispc::WrapMode wrapS = wrapMode(request.uAddressMode, outsideS);
  const ispc::WrapMode wrapT = wrapMode(request.vAddressMode, outsideT);
  const float s = request.st[0];
  const float t = request.st[1];
  if ((outsideS && (s < 0.f || s > 1.f)) || (outsideT && (t < 0.f || t > 1.f))
      || !std::isfinite(s) || !std::isfinite(t))
    return defaultResult(request, mxcpp::TextureSampleStatus::Ok);

  // the mip level of the request's footprint, in level 0's texels
  const ImageLevel &base = image.levels.front();
  const auto texels = [&](const mxcpp::Vec2f &d) {
    return std::hypot(d[0] * float(base.width), d[1] * float(base.height));
  };
  const float footprint = std::max(texels(request.dstdx), texels(request.dstdy));
  const bool linear =
      request.filterType != mxcpp::TextureFilterType::Closest;
  const int last = int(image.levels.size()) - 1;
  float level = footprint > 1.f && std::isfinite(footprint)
      ? std::min(std::log2(footprint), float(last))
      : 0.f;
  if (!linear)
    level = std::round(level);

  const auto sampleLevel = [&](int l, float *out) {
    const ImageLevel &lv = image.levels[size_t(l)];
    ispc::HalcyonTexture_sampleImage(lv.texels.data(),
        lv.width,
        lv.height,
        wrapS,
        wrapT,
        linear,
        s,
        t,
        out);
  };
  float v[4];
  const int l0 = int(std::floor(level));
  sampleLevel(l0, v);
  const float f = level - float(l0);
  if (f > 0.f && l0 < last) {
    float w[4];
    sampleLevel(l0 + 1, w);
    for (int c = 0; c < 4; c++)
      v[c] += f * (w[c] - v[c]);
  }
  if (!image.hasAlpha)
    v[3] = request.channelFillValue;

  // colour data into the working space, after filtering, as Typhoon's
  // backend (_ApplyColorTransform())
  if (request.dataRole == mxcpp::TextureDataRole::Color
      && request.channelCount >= 3 && !request.sourceColorSpace.empty()) {
    mxcpp::Vec3f rgb(v[0], v[1], v[2]);
    if (convertToWorkingSpace(request.sourceColorSpace, rgb)) {
      v[0] = rgb[0];
      v[1] = rgb[1];
      v[2] = rgb[2];
    }
  }

  mxcpp::Texture2DResult result;
  result.value = mxcpp::Vec4f(v[0], v[1], v[2], v[3]);
  result.status = mxcpp::TextureSampleStatus::Ok;
  return result;
}

} // namespace halcyon::pathtracer
