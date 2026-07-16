// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "HelideGPUDeviceGlobalState.h"
#include "GLContextInterface.h"

// Shader format helper
#include "gpu/ShaderData.h"

// Generated SPIR-V headers (produced at build time by cmake/bin2header.cmake)
#include "sphere_frag_spv.h"
#include "sphere_vert_spv.h"
#include "triangle_frag_spv.h"
#include "triangle_mesh_vert_spv.h"
#include "triangle_vert_spv.h"

#include "volume_frag_spv.h"
#include "volume_vert_spv.h"

#include "blur_frag_spv.h"
#include "composite_frag_spv.h"
#include "fullscreen_vert_spv.h"
#include "ssao_frag_spv.h"

// Generated MSL headers (when spirv-cross is available at build time)
#ifdef HELIDE_GPU_HAS_MSL_SHADERS
#include "sphere_frag_msl.h"
#include "sphere_vert_msl.h"
#include "triangle_frag_msl.h"
#include "triangle_mesh_vert_msl.h"
#include "triangle_vert_msl.h"
#include "volume_frag_msl.h"
#include "volume_vert_msl.h"
#include "blur_frag_msl.h"
#include "composite_frag_msl.h"
#include "fullscreen_vert_msl.h"
#include "ssao_frag_msl.h"
#endif

namespace helide_gpu {

namespace {

// clang-format off
#ifdef HELIDE_GPU_HAS_MSL_SHADERS
#define SHADER_DATA(name) {name##_spv, name##_spv_len, name##_msl, name##_msl_len}
#else
#define SHADER_DATA(name) {name##_spv, name##_spv_len}
#endif

const ShaderData kTriangleVert     = SHADER_DATA(triangle_vert);
const ShaderData kTriangleMeshVert = SHADER_DATA(triangle_mesh_vert);
const ShaderData kTriangleFrag     = SHADER_DATA(triangle_frag);
const ShaderData kSphereVert       = SHADER_DATA(sphere_vert);
const ShaderData kSphereFrag       = SHADER_DATA(sphere_frag);
const ShaderData kVolumeVert       = SHADER_DATA(volume_vert);
const ShaderData kVolumeFrag       = SHADER_DATA(volume_frag);
const ShaderData kFullscreenVert   = SHADER_DATA(fullscreen_vert);
const ShaderData kSsaoFrag         = SHADER_DATA(ssao_frag);
const ShaderData kBlurFrag         = SHADER_DATA(blur_frag);
const ShaderData kCompositeFrag    = SHADER_DATA(composite_frag);

#undef SHADER_DATA
// clang-format on

} // namespace

HelideGPUDeviceGlobalState::HelideGPUDeviceGlobalState(ANARIDevice d)
    : helium::BaseGlobalDeviceState(d), device(d)
{}

void HelideGPUDeviceGlobalState::gpu_initPipelines()
{
  auto *dev = gpu.device;
  if (!dev)
    return;

  auto fmt = gpu.sdlDevice.shaderFormat;

  // --- Triangle SSBO pipeline ---
  {
    SDL_GPUShaderCreateInfo vertInfo{};
    fillShaderInfo(vertInfo, kTriangleVert, fmt, SDL_GPU_SHADERSTAGE_VERTEX);
    vertInfo.num_storage_buffers = 9;
    vertInfo.num_uniform_buffers = 1;

    auto *vert = SDL_CreateGPUShader(dev, &vertInfo);
    if (!vert) {
      anariDeviceReportStatus(device, ANARI_SEVERITY_ERROR,
          ANARI_STATUS_UNKNOWN_ERROR,
          "[SDL3_gpu] Failed to create vertex shader: %s", SDL_GetError());
      return;
    }

    SDL_GPUShaderCreateInfo fragInfo{};
    fillShaderInfo(fragInfo, kTriangleFrag, fmt, SDL_GPU_SHADERSTAGE_FRAGMENT);
    fragInfo.num_samplers = 1;
    fragInfo.num_uniform_buffers = 4;

    auto *frag = SDL_CreateGPUShader(dev, &fragInfo);
    if (!frag) {
      anariDeviceReportStatus(device, ANARI_SEVERITY_ERROR,
          ANARI_STATUS_UNKNOWN_ERROR,
          "[SDL3_gpu] Failed to create fragment shader: %s", SDL_GetError());
      SDL_ReleaseGPUShader(dev, vert);
      return;
    }

    SDL_GPUVertexBufferDescription vbDesc{};
    vbDesc.slot = 0;
    vbDesc.pitch = sizeof(float) * 3;
    vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute vAttr{};
    vAttr.location = 0;
    vAttr.buffer_slot = 0;
    vAttr.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;

    SDL_GPUVertexInputState vertexInput{};
    vertexInput.vertex_buffer_descriptions = &vbDesc;
    vertexInput.num_vertex_buffers = 1;
    vertexInput.vertex_attributes = &vAttr;
    vertexInput.num_vertex_attributes = 1;

    SDL_GPUColorTargetDescription colorDescs[4]{};
    colorDescs[0].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    colorDescs[0].blend_state.enable_color_write_mask = true;
    colorDescs[0].blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R
        | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B
        | SDL_GPU_COLORCOMPONENT_A;
    colorDescs[1].format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_UINT;
    colorDescs[2].format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    colorDescs[3].format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;

    SDL_GPUGraphicsPipelineTargetInfo targetInfo{};
    targetInfo.color_target_descriptions = colorDescs;
    targetInfo.num_color_targets = 4;
    targetInfo.has_depth_stencil_target = true;
    targetInfo.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

    SDL_GPUDepthStencilState depthState{};
    depthState.compare_op = SDL_GPU_COMPAREOP_LESS;
    depthState.enable_depth_test = true;
    depthState.enable_depth_write = true;

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = vert;
    pipelineInfo.fragment_shader = frag;
    pipelineInfo.vertex_input_state = vertexInput;
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.depth_stencil_state = depthState;
    pipelineInfo.target_info = targetInfo;

    gpu.trianglePipeline = SDL_CreateGPUGraphicsPipeline(dev, &pipelineInfo);
    if (!gpu.trianglePipeline) {
      anariDeviceReportStatus(device, ANARI_SEVERITY_ERROR,
          ANARI_STATUS_UNKNOWN_ERROR,
          "[SDL3_gpu] Failed to create triangle pipeline: %s", SDL_GetError());
    }

    SDL_ReleaseGPUShader(dev, vert);
    SDL_ReleaseGPUShader(dev, frag);
  }

  // --- Triangle mesh pipeline ---
  {
    SDL_GPUShaderCreateInfo vertInfo{};
    fillShaderInfo(vertInfo, kTriangleMeshVert, fmt, SDL_GPU_SHADERSTAGE_VERTEX);
    vertInfo.num_storage_buffers = 7;
    vertInfo.num_uniform_buffers = 1;

    auto *vert = SDL_CreateGPUShader(dev, &vertInfo);
    if (!vert) {
      anariDeviceReportStatus(device, ANARI_SEVERITY_ERROR,
          ANARI_STATUS_UNKNOWN_ERROR,
          "[SDL3_gpu] Failed to create triangle mesh vertex shader: %s",
          SDL_GetError());
      return;
    }

    SDL_GPUShaderCreateInfo fragInfo{};
    fillShaderInfo(fragInfo, kTriangleFrag, fmt, SDL_GPU_SHADERSTAGE_FRAGMENT);
    fragInfo.num_samplers = 1;
    fragInfo.num_uniform_buffers = 4;

    auto *frag = SDL_CreateGPUShader(dev, &fragInfo);
    if (!frag) {
      anariDeviceReportStatus(device, ANARI_SEVERITY_ERROR,
          ANARI_STATUS_UNKNOWN_ERROR,
          "[SDL3_gpu] Failed to create triangle mesh fragment shader: %s",
          SDL_GetError());
      SDL_ReleaseGPUShader(dev, vert);
      return;
    }

    SDL_GPUVertexBufferDescription vbDesc{};
    vbDesc.slot = 0;
    vbDesc.pitch = sizeof(float) * 6;
    vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute vAttrs[2]{};
    vAttrs[0].location = 0;
    vAttrs[0].buffer_slot = 0;
    vAttrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vAttrs[0].offset = 0;
    vAttrs[1].location = 1;
    vAttrs[1].buffer_slot = 0;
    vAttrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vAttrs[1].offset = sizeof(float) * 3;

    SDL_GPUVertexInputState vertexInput{};
    vertexInput.vertex_buffer_descriptions = &vbDesc;
    vertexInput.num_vertex_buffers = 1;
    vertexInput.vertex_attributes = vAttrs;
    vertexInput.num_vertex_attributes = 2;

    SDL_GPUColorTargetDescription colorDescs[4]{};
    colorDescs[0].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    colorDescs[0].blend_state.enable_color_write_mask = true;
    colorDescs[0].blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R
        | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B
        | SDL_GPU_COLORCOMPONENT_A;
    colorDescs[1].format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_UINT;
    colorDescs[2].format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    colorDescs[3].format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;

    SDL_GPUGraphicsPipelineTargetInfo targetInfo{};
    targetInfo.color_target_descriptions = colorDescs;
    targetInfo.num_color_targets = 4;
    targetInfo.has_depth_stencil_target = true;
    targetInfo.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

    SDL_GPUDepthStencilState depthState{};
    depthState.compare_op = SDL_GPU_COMPAREOP_LESS;
    depthState.enable_depth_test = true;
    depthState.enable_depth_write = true;

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = vert;
    pipelineInfo.fragment_shader = frag;
    pipelineInfo.vertex_input_state = vertexInput;
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.depth_stencil_state = depthState;
    pipelineInfo.target_info = targetInfo;

    gpu.triangleMeshPipeline = SDL_CreateGPUGraphicsPipeline(dev, &pipelineInfo);
    if (!gpu.triangleMeshPipeline) {
      anariDeviceReportStatus(device, ANARI_SEVERITY_ERROR,
          ANARI_STATUS_UNKNOWN_ERROR,
          "[SDL3_gpu] Failed to create triangle mesh pipeline: %s",
          SDL_GetError());
    }

    SDL_ReleaseGPUShader(dev, vert);
    SDL_ReleaseGPUShader(dev, frag);
  }

  // --- Sphere pipeline ---
  {
    SDL_GPUShaderCreateInfo vertInfo{};
    fillShaderInfo(vertInfo, kSphereVert, fmt, SDL_GPU_SHADERSTAGE_VERTEX);
    vertInfo.num_storage_buffers = 7;
    vertInfo.num_uniform_buffers = 1;

    auto *vert = SDL_CreateGPUShader(dev, &vertInfo);
    if (!vert) {
      anariDeviceReportStatus(device, ANARI_SEVERITY_ERROR,
          ANARI_STATUS_UNKNOWN_ERROR,
          "[SDL3_gpu] Failed to create sphere vertex shader: %s",
          SDL_GetError());
      return;
    }

    SDL_GPUShaderCreateInfo fragInfo{};
    fillShaderInfo(fragInfo, kSphereFrag, fmt, SDL_GPU_SHADERSTAGE_FRAGMENT);
    fragInfo.num_samplers = 1;
    fragInfo.num_uniform_buffers = 4;

    auto *frag = SDL_CreateGPUShader(dev, &fragInfo);
    if (!frag) {
      anariDeviceReportStatus(device, ANARI_SEVERITY_ERROR,
          ANARI_STATUS_UNKNOWN_ERROR,
          "[SDL3_gpu] Failed to create sphere fragment shader: %s",
          SDL_GetError());
      SDL_ReleaseGPUShader(dev, vert);
      return;
    }

    SDL_GPUVertexBufferDescription vbDesc{};
    vbDesc.slot = 0;
    vbDesc.pitch = sizeof(float) * 3;
    vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute vAttr{};
    vAttr.location = 0;
    vAttr.buffer_slot = 0;
    vAttr.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;

    SDL_GPUVertexInputState vertexInput{};
    vertexInput.vertex_buffer_descriptions = &vbDesc;
    vertexInput.num_vertex_buffers = 1;
    vertexInput.vertex_attributes = &vAttr;
    vertexInput.num_vertex_attributes = 1;

    SDL_GPUColorTargetDescription colorDescs[4]{};
    colorDescs[0].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    colorDescs[0].blend_state.enable_color_write_mask = true;
    colorDescs[0].blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R
        | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B
        | SDL_GPU_COLORCOMPONENT_A;
    colorDescs[1].format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_UINT;
    colorDescs[2].format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    colorDescs[3].format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;

    SDL_GPUGraphicsPipelineTargetInfo targetInfo{};
    targetInfo.color_target_descriptions = colorDescs;
    targetInfo.num_color_targets = 4;
    targetInfo.has_depth_stencil_target = true;
    targetInfo.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

    SDL_GPUDepthStencilState depthState{};
    depthState.compare_op = SDL_GPU_COMPAREOP_LESS;
    depthState.enable_depth_test = true;
    depthState.enable_depth_write = true;

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = vert;
    pipelineInfo.fragment_shader = frag;
    pipelineInfo.vertex_input_state = vertexInput;
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.depth_stencil_state = depthState;
    pipelineInfo.target_info = targetInfo;

    gpu.spherePipeline = SDL_CreateGPUGraphicsPipeline(dev, &pipelineInfo);
    if (!gpu.spherePipeline) {
      anariDeviceReportStatus(device, ANARI_SEVERITY_ERROR,
          ANARI_STATUS_UNKNOWN_ERROR,
          "[SDL3_gpu] Failed to create sphere pipeline: %s", SDL_GetError());
    }

    SDL_ReleaseGPUShader(dev, vert);
    SDL_ReleaseGPUShader(dev, frag);
  }

  // --- Volume pipeline ---
  {
    SDL_GPUShaderCreateInfo vertInfo{};
    fillShaderInfo(vertInfo, kVolumeVert, fmt, SDL_GPU_SHADERSTAGE_VERTEX);
    vertInfo.num_uniform_buffers = 1;

    auto *vert = SDL_CreateGPUShader(dev, &vertInfo);
    if (!vert) {
      anariDeviceReportStatus(device, ANARI_SEVERITY_ERROR,
          ANARI_STATUS_UNKNOWN_ERROR,
          "[SDL3_gpu] Failed to create volume vertex shader: %s",
          SDL_GetError());
      return;
    }

    SDL_GPUShaderCreateInfo fragInfo{};
    fillShaderInfo(fragInfo, kVolumeFrag, fmt, SDL_GPU_SHADERSTAGE_FRAGMENT);
    fragInfo.num_samplers = 2;
    fragInfo.num_uniform_buffers = 1;

    auto *frag = SDL_CreateGPUShader(dev, &fragInfo);
    if (!frag) {
      anariDeviceReportStatus(device, ANARI_SEVERITY_ERROR,
          ANARI_STATUS_UNKNOWN_ERROR,
          "[SDL3_gpu] Failed to create volume fragment shader: %s",
          SDL_GetError());
      SDL_ReleaseGPUShader(dev, vert);
      return;
    }

    SDL_GPUVertexBufferDescription vbDesc{};
    vbDesc.slot = 0;
    vbDesc.pitch = sizeof(float) * 3;
    vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute vAttr{};
    vAttr.location = 0;
    vAttr.buffer_slot = 0;
    vAttr.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;

    SDL_GPUVertexInputState vertexInput{};
    vertexInput.vertex_buffer_descriptions = &vbDesc;
    vertexInput.num_vertex_buffers = 1;
    vertexInput.vertex_attributes = &vAttr;
    vertexInput.num_vertex_attributes = 1;

    SDL_GPUColorTargetDescription colorDescs[4]{};
    colorDescs[0].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    colorDescs[0].blend_state.enable_blend = true;
    colorDescs[0].blend_state.src_color_blendfactor =
        SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    colorDescs[0].blend_state.dst_color_blendfactor =
        SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colorDescs[0].blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    colorDescs[0].blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colorDescs[0].blend_state.dst_alpha_blendfactor =
        SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colorDescs[0].blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    colorDescs[0].blend_state.enable_color_write_mask = true;
    colorDescs[0].blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R
        | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B
        | SDL_GPU_COLORCOMPONENT_A;
    colorDescs[1].format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_UINT;
    colorDescs[2].format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    colorDescs[3].format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;

    SDL_GPUGraphicsPipelineTargetInfo targetInfo{};
    targetInfo.color_target_descriptions = colorDescs;
    targetInfo.num_color_targets = 4;
    targetInfo.has_depth_stencil_target = true;
    targetInfo.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

    SDL_GPUDepthStencilState depthState{};
    depthState.compare_op = SDL_GPU_COMPAREOP_LESS;
    depthState.enable_depth_test = true;
    depthState.enable_depth_write = false;

    SDL_GPURasterizerState rasterState{};
    rasterState.cull_mode = SDL_GPU_CULLMODE_FRONT;
    rasterState.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    rasterState.fill_mode = SDL_GPU_FILLMODE_FILL;

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = vert;
    pipelineInfo.fragment_shader = frag;
    pipelineInfo.vertex_input_state = vertexInput;
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.depth_stencil_state = depthState;
    pipelineInfo.rasterizer_state = rasterState;
    pipelineInfo.target_info = targetInfo;

    gpu.volumePipeline = SDL_CreateGPUGraphicsPipeline(dev, &pipelineInfo);
    if (!gpu.volumePipeline) {
      anariDeviceReportStatus(device, ANARI_SEVERITY_ERROR,
          ANARI_STATUS_UNKNOWN_ERROR,
          "[SDL3_gpu] Failed to create volume pipeline: %s", SDL_GetError());
    }

    SDL_ReleaseGPUShader(dev, vert);
    SDL_ReleaseGPUShader(dev, frag);
  }

  // --- SSAO / blur / composite pipelines ---
  {
    SDL_GPUShaderCreateInfo fsVertInfo{};
    fillShaderInfo(fsVertInfo, kFullscreenVert, fmt, SDL_GPU_SHADERSTAGE_VERTEX);
    fsVertInfo.num_uniform_buffers = 0;

    auto *fsVert = SDL_CreateGPUShader(dev, &fsVertInfo);
    if (!fsVert) {
      anariDeviceReportStatus(device, ANARI_SEVERITY_ERROR,
          ANARI_STATUS_UNKNOWN_ERROR,
          "[SDL3_gpu] Failed to create fullscreen vertex shader: %s",
          SDL_GetError());
      return;
    }

    SDL_GPUVertexInputState emptyVertexInput{};

    // SSAO
    {
      SDL_GPUShaderCreateInfo fragInfo{};
      fillShaderInfo(fragInfo, kSsaoFrag, fmt, SDL_GPU_SHADERSTAGE_FRAGMENT);
      fragInfo.num_samplers = 4;
      fragInfo.num_uniform_buffers = 1;

      auto *frag = SDL_CreateGPUShader(dev, &fragInfo);
      if (!frag) {
        anariDeviceReportStatus(device, ANARI_SEVERITY_ERROR,
            ANARI_STATUS_UNKNOWN_ERROR,
            "[SDL3_gpu] Failed to create SSAO fragment shader: %s",
            SDL_GetError());
        SDL_ReleaseGPUShader(dev, fsVert);
        return;
      }

      SDL_GPUColorTargetDescription colorDesc{};
      colorDesc.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

      SDL_GPUGraphicsPipelineTargetInfo targetInfo{};
      targetInfo.color_target_descriptions = &colorDesc;
      targetInfo.num_color_targets = 1;

      SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
      pipelineInfo.vertex_shader = fsVert;
      pipelineInfo.fragment_shader = frag;
      pipelineInfo.vertex_input_state = emptyVertexInput;
      pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
      pipelineInfo.target_info = targetInfo;

      gpu.ssaoPipeline = SDL_CreateGPUGraphicsPipeline(dev, &pipelineInfo);
      if (!gpu.ssaoPipeline) {
        anariDeviceReportStatus(device, ANARI_SEVERITY_ERROR,
            ANARI_STATUS_UNKNOWN_ERROR,
            "[SDL3_gpu] Failed to create SSAO pipeline: %s", SDL_GetError());
      }

      SDL_ReleaseGPUShader(dev, frag);
    }

    // Blur
    {
      SDL_GPUShaderCreateInfo fragInfo{};
      fillShaderInfo(fragInfo, kBlurFrag, fmt, SDL_GPU_SHADERSTAGE_FRAGMENT);
      fragInfo.num_samplers = 3;
      fragInfo.num_uniform_buffers = 1;

      auto *frag = SDL_CreateGPUShader(dev, &fragInfo);
      if (!frag) {
        anariDeviceReportStatus(device, ANARI_SEVERITY_ERROR,
            ANARI_STATUS_UNKNOWN_ERROR,
            "[SDL3_gpu] Failed to create blur fragment shader: %s",
            SDL_GetError());
        SDL_ReleaseGPUShader(dev, fsVert);
        return;
      }

      SDL_GPUColorTargetDescription colorDesc{};
      colorDesc.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

      SDL_GPUGraphicsPipelineTargetInfo targetInfo{};
      targetInfo.color_target_descriptions = &colorDesc;
      targetInfo.num_color_targets = 1;

      SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
      pipelineInfo.vertex_shader = fsVert;
      pipelineInfo.fragment_shader = frag;
      pipelineInfo.vertex_input_state = emptyVertexInput;
      pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
      pipelineInfo.target_info = targetInfo;

      gpu.blurPipeline = SDL_CreateGPUGraphicsPipeline(dev, &pipelineInfo);
      if (!gpu.blurPipeline) {
        anariDeviceReportStatus(device, ANARI_SEVERITY_ERROR,
            ANARI_STATUS_UNKNOWN_ERROR,
            "[SDL3_gpu] Failed to create blur pipeline: %s", SDL_GetError());
      }

      SDL_ReleaseGPUShader(dev, frag);
    }

    // Composite
    {
      SDL_GPUShaderCreateInfo fragInfo{};
      fillShaderInfo(fragInfo, kCompositeFrag, fmt, SDL_GPU_SHADERSTAGE_FRAGMENT);
      fragInfo.num_samplers = 2;
      fragInfo.num_uniform_buffers = 1;

      auto *frag = SDL_CreateGPUShader(dev, &fragInfo);
      if (!frag) {
        anariDeviceReportStatus(device, ANARI_SEVERITY_ERROR,
            ANARI_STATUS_UNKNOWN_ERROR,
            "[SDL3_gpu] Failed to create composite fragment shader: %s",
            SDL_GetError());
        SDL_ReleaseGPUShader(dev, fsVert);
        return;
      }

      SDL_GPUColorTargetDescription colorDesc{};
      colorDesc.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

      SDL_GPUGraphicsPipelineTargetInfo targetInfo{};
      targetInfo.color_target_descriptions = &colorDesc;
      targetInfo.num_color_targets = 1;

      SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
      pipelineInfo.vertex_shader = fsVert;
      pipelineInfo.fragment_shader = frag;
      pipelineInfo.vertex_input_state = emptyVertexInput;
      pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
      pipelineInfo.target_info = targetInfo;

      gpu.compositePipeline = SDL_CreateGPUGraphicsPipeline(dev, &pipelineInfo);
      if (!gpu.compositePipeline) {
        anariDeviceReportStatus(device, ANARI_SEVERITY_ERROR,
            ANARI_STATUS_UNKNOWN_ERROR,
            "[SDL3_gpu] Failed to create composite pipeline: %s",
            SDL_GetError());
      }

      SDL_ReleaseGPUShader(dev, frag);
    }

    SDL_ReleaseGPUShader(dev, fsVert);
  }
}

void HelideGPUDeviceGlobalState::gpu_destroyPipelines()
{
  auto *dev = gpu.device;
  if (!dev)
    return;

  auto releaseP = [dev](SDL_GPUGraphicsPipeline *&p) {
    if (p) {
      SDL_ReleaseGPUGraphicsPipeline(dev, p);
      p = nullptr;
    }
  };

  releaseP(gpu.compositePipeline);
  releaseP(gpu.blurPipeline);
  releaseP(gpu.ssaoPipeline);
  releaseP(gpu.volumePipeline);
  releaseP(gpu.spherePipeline);
  releaseP(gpu.triangleMeshPipeline);
  releaseP(gpu.trianglePipeline);
}

} // namespace helide_gpu
