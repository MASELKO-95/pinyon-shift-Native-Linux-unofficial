#include "native_renderer/native_output_track.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/graphics/d3d12/deferred_command_list.h>
#include <rex/logging.h>
#include <rex/system/interfaces/graphics.h>

#include "native_renderer/ordered_ui_capture.h"
#include "native_renderer/snr04_owned_scene_diagnostic.h"

REXCVAR_DEFINE_BOOL(pinyon_shift_native_downsample_probe, false,
                    "Pinyon Shift",
                    "Show the selected native 320x192 post-scene target")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace pinyon_shift::native_renderer {
namespace {
using Microsoft::WRL::ComPtr;
using PipelineKey = std::tuple<uint64_t, uint64_t, uint32_t, uint32_t, uint32_t,
    uint32_t>;
using RemainderPipelineKey = std::tuple<uint64_t, uint64_t, uint32_t,
    uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t>;
using ProducerPipelineKey = std::tuple<uint64_t, uint64_t, uint64_t,
    uint64_t, uint32_t, uint32_t, uint32_t, bool>;

uint32_t TrackMaterialKind(const Snr04TrackDraw& draw) {
  if (draw.shader == 0x0CBC533419F61E0Dull &&
      draw.pixel_shader == 0x56D45C45966FD938ull &&
      draw.pixel_specialization == 0x4000002B003Full &&
      draw.textures.size() == 5)
    return 3;
  if (((draw.shader == 0x0CBC533419F61E0Dull &&
        draw.pixel_shader == 0xEFCA69AA2BEE366Bull) ||
       (draw.shader == 0x5DB1ECF39EA11DB0ull &&
        draw.pixel_shader == 0x6508BAC22C4E1720ull)) &&
      draw.pixel_specialization == 0x4000002B003Full &&
      draw.textures.size() == 3)
    return 3;
  if (draw.shader == 0x6934E161812AB10Bull &&
      draw.pixel_shader == 0xB98566FB7CE14699ull &&
      draw.pixel_specialization == 0x4000005B007Full &&
      draw.textures.size() == 6)
    return 3;
  if (draw.pixel_shader == 0x6F7CDE74CDACCB08ull &&
      draw.shader == 0x07425D208E8BD688ull &&
      draw.specialization == 0x7Full) return 1;
  if (draw.pixel_shader == 0x93961AB9BDF347DDull &&
      draw.shader == 0x1193B16753866698ull &&
      draw.specialization == 0x3FFull) return 2;
  return 0;
}

std::array<uint32_t, 6> TrackShaderFetches(const Snr04TrackDraw& draw) {
  if (draw.pixel_shader == 0x56D45C45966FD938ull)
    return {5, 7, 13, 4, 0};
  if (draw.pixel_shader == 0xB98566FB7CE14699ull)
    return {6, 13, 2, 5, 1, 0};
  return draw.pixel_shader == 0x6508BAC22C4E1720ull
      ? std::array<uint32_t, 6>{13, 3, 0}
      : std::array<uint32_t, 6>{5, 13, 0};
}

uint32_t RemainderMaterialKind(const Snr04RemainderDraw& draw) {
  if (draw.family == 1 && draw.shader == 0x2E5E0A854BE00027ull &&
      draw.pixel_shader == 0xBDFFA72B7ED2FBA4ull &&
      draw.pixel_specialization == 0x16003Full && draw.texture_count == 5)
    return 4;
  if (draw.family == 1 && draw.shader == 0xD34A83D9E6B3A399ull &&
      draw.pixel_shader == 0xE9CD565D9C61D037ull &&
      draw.pixel_specialization == 0x16003Full && draw.texture_count == 8)
    return 3;
  if (draw.family == 3 && draw.shader == 0x0DF9CA19A93A75D9ull &&
      draw.pixel_shader == 0xE349204378CA1591ull) return 1;
  if (draw.family == 1 && draw.shader == 0xCC2F3F4B3FBA53F5ull &&
      draw.pixel_shader == 0xCDA93D7ADC1991D8ull &&
      draw.pixel_specialization == 0x10001ull && !draw.texture_count) return 2;
  return 0;
}

const uint32_t* RemainderPixelConstant(const Snr04RemainderDraw& draw,
                                       uint32_t reg) {
  if (!(draw.pixel_bitmap[reg / 64] & (uint64_t(1) << (reg % 64))))
    return nullptr;
  uint32_t offset = 0;
  for (uint32_t i = 0; i < reg; ++i)
    offset += (draw.pixel_bitmap[i / 64] >> (i % 64)) & 1;
  offset *= 4;
  return offset + 4 <= draw.pixel_packed.size()
      ? draw.pixel_packed.data() + offset : nullptr;
}

struct UploadArena {
  std::vector<uint8_t> bytes;

  uint64_t Add(const void* source, size_t size) {
    const size_t offset = (bytes.size() + 255) & ~size_t(255);
    if (offset > 64 * 1024 * 1024 ||
        size > 64 * 1024 * 1024 - offset)
      throw std::runtime_error("native track upload exceeds 64 MiB");
    bytes.resize(offset + size);
    std::memcpy(bytes.data() + offset, source, size);
    return offset;
  }
};

struct TrackFrame {
  ComPtr<ID3D12Resource> upload, depth, color, color_tiles, downsample,
      offscreen, offscreen_depth, hud;
  std::array<ComPtr<ID3D12Resource>, 2> initial_color_versions;
  std::array<ComPtr<ID3D12Resource>, 3> depth_versions;
  ComPtr<ID3D12DescriptorHeap> rtv, dsv, srv, materials, samplers;
  std::vector<ComPtr<ID3D12Resource>> material_resources;
};

struct TrackDrawBinding {
  PipelineKey pipeline;
  uint64_t vertex = 0, index = 0, b0 = 0, b1 = 0, b3 = 0;
  uint64_t pixel_constants = 0, pixel_bool = 0, pixel_descriptors = 0;
  uint32_t material_index = UINT32_MAX;
  std::array<uint32_t, 6> shader_materials{};
  uint32_t shader_texture_count = 0;
  uint32_t shader_view_offset = 0, shader_sampler_offset = 0;
  D3D12_VIEWPORT viewport{};
  D3D12_RECT scissor{};
};

struct ItemDrawBinding {
  uint64_t sequence = 0, shader = 0, specialization = 0;
  uint32_t count = 0;
  uint64_t vertex = 0, b0 = 0, b1 = 0, b3 = 0;
  uint32_t material_index = UINT32_MAX;
  bool alpha = false;
  D3D12_VIEWPORT viewport{};
  D3D12_RECT scissor{};
};

struct ManagerDrawBinding {
  uint64_t sequence = 0;
  uint32_t count = 0, index_bytes = 0;
  uint64_t index = 0, b0 = 0, b1 = 0, b3 = 0;
  uint64_t b4 = 0, pixel_constants = 0, pixel_bool = 0;
  std::array<uint32_t, 2> shader_materials{};
  uint32_t shader_view_offset = 0;
  D3D12_VIEWPORT viewport{};
  D3D12_RECT scissor{};
};

struct RemainderDrawBinding {
  RemainderPipelineKey pipeline;
  uint64_t sequence = 0;
  uint64_t index = 0, b0 = 0, b1 = 0, b3 = 0, b4 = 0;
  uint64_t pixel_constants = 0, shader_bool = 0;
  uint32_t count = 0, index_bytes = 0, format = 0, primitive = 0;
  uint32_t family = 0;
  uint32_t material_index = UINT32_MAX;
  std::array<uint32_t, 8> shader_materials{};
  uint32_t shader_view_offset = 0, shader_sampler_offset = 0;
  std::array<float, 4> color{};
  D3D12_VIEWPORT viewport{};
  D3D12_RECT scissor{};
};

struct ProducerDrawBinding {
  uint64_t sequence = 0;
  ProducerPipelineKey pipeline;
  uint64_t index = 0, vertex = 0, b0 = 0, b1 = 0, b3 = 0;
  uint64_t descriptors = 0, pixel_constants = 0, bool_loop = 0;
  uint32_t count = 0, material_count = 0, view_offset = 0;
  std::array<uint32_t, 2> materials{};
  bool feedback = false, indexed = true, downsample = false;
  D3D12_VIEWPORT viewport{};
  D3D12_RECT scissor{};
};

struct TrackGraphics {
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12RootSignature> root;
  ComPtr<ID3DBlob> pixel, pixel_textured, pixel_road, pixel_foliage,
      pixel_car, rectangle_geometry;
  ComPtr<ID3D12PipelineState> manager_pipeline, manager_original_pipeline;
  ComPtr<ID3D12RootSignature> blit_root;
  ComPtr<ID3D12PipelineState> blit_pipeline, scene_blit_pipeline,
      seed_pipeline;
  std::map<PipelineKey, ComPtr<ID3D12PipelineState>> pipelines;
  std::map<std::tuple<uint64_t, uint64_t, bool>,
           ComPtr<ID3D12PipelineState>> item_pipelines;
  std::map<RemainderPipelineKey, ComPtr<ID3D12PipelineState>> remainder_pipelines;
  std::map<ProducerPipelineKey, ComPtr<ID3D12PipelineState>> producer_pipelines;
  std::deque<std::pair<uint64_t, TrackFrame>> submitted;

  bool Ready(ID3D12Device* current) {
    if (device.Get() != current) {
      submitted.clear();
      pipelines.clear();
      item_pipelines.clear();
      remainder_pipelines.clear();
      producer_pipelines.clear();
      root.Reset();
      pixel.Reset();
      pixel_textured.Reset();
      pixel_road.Reset();
      pixel_foliage.Reset();
      pixel_car.Reset();
      rectangle_geometry.Reset();
      manager_pipeline.Reset();
      manager_original_pipeline.Reset();
      blit_root.Reset();
      blit_pipeline.Reset();
      scene_blit_pipeline.Reset();
      seed_pipeline.Reset();
      device = current;
    }
    if (root && pixel && pixel_textured && pixel_road && pixel_foliage &&
        pixel_car)
      return true;
    constexpr char shader[] =
        "cbuffer Color : register(b5) { float4 flat; };"
        "float4 main() : SV_Target0 { return flat; }";
    ComPtr<ID3DBlob> errors, serialized;
    if (FAILED(D3DCompile(shader, sizeof(shader) - 1, nullptr, nullptr,
                          nullptr, "main", "ps_5_1", 0, 0, &pixel, &errors)))
      return false;
    constexpr char textured_shader[] =
        "Texture2D<float4> albedo : register(t1);"
        "SamplerState linear_wrap : register(s0);"
        "float4 main(float4 uv[5] : TEXCOORD0) : SV_Target0 {"
        " return float4(albedo.Sample(linear_wrap, uv[4].xy).rgb, 1); }";
    if (FAILED(D3DCompile(textured_shader, sizeof(textured_shader) - 1,
                          nullptr, nullptr, nullptr, "main", "ps_5_1", 0, 0,
                          &pixel_textured, &errors)))
      return false;
    constexpr char road_shader[] =
        "Texture2D<float4> albedo : register(t1);"
        "SamplerState linear_wrap : register(s0);"
        "float4 main(float4 uv[3] : TEXCOORD0) : SV_Target0 {"
        " return float4(albedo.Sample(linear_wrap, uv[2].xy).rgb, 1); }";
    if (FAILED(D3DCompile(road_shader, sizeof(road_shader) - 1,
                          nullptr, nullptr, nullptr, "main", "ps_5_1", 0, 0,
                          &pixel_road, &errors)))
      return false;
    constexpr char foliage_shader[] =
        "Texture2D<float4> foliage_tex : register(t1);"
        "SamplerState clamp_sampler : register(s1);"
        "float4 main(float4 varying[5] : TEXCOORD0) : SV_Target0 {"
        " float2 uv = varying[0].xy + 0.001465 / 256.0;"
        " float4 texel = foliage_tex.Sample(clamp_sampler, uv);"
        " clip(texel.a * varying[4].w - 0.5);"
        " return float4(texel.rgb, 1); }";
    if (FAILED(D3DCompile(foliage_shader, sizeof(foliage_shader) - 1,
                          nullptr, nullptr, nullptr, "main", "ps_5_1", 0, 0,
                          &pixel_foliage, &errors)))
      return false;
    constexpr char car_shader[] =
        "Texture2D<float4> mask_tex : register(t1);"
        "SamplerState clamp_sampler : register(s1);"
        "cbuffer Material : register(b4) {"
        " float4 c0, c1, c2, c3, c255; };"
        "float4 main(float4 varying[2] : TEXCOORD0) : SV_Target0 {"
        " float2 uv = varying[0].xy + 0.001465 / 64.0;"
        " float mask = mask_tex.Sample(clamp_sampler, uv).r;"
        " float2 edge = saturate((c255.x - varying[0].xy) / c1.xy) *"
        "               saturate(varying[0].xy / c1.xy);"
        " float factor = c255.x + (c3.z - c255.x) * saturate(varying[1].x);"
        " float alpha = saturate(mask * factor) * varying[0].z *"
        "               edge.x * edge.y;"
        " return float4(sqrt(abs(c0.rgb + c2.rgb * varying[0].w)),"
        "               alpha); }";
    if (FAILED(D3DCompile(car_shader, sizeof(car_shader) - 1,
                          nullptr, nullptr, nullptr, "main", "ps_5_1", 0, 0,
                          &pixel_car, &errors)))
      return false;
    D3D12_ROOT_PARAMETER parameters[13]{};
    for (uint32_t i = 0; i < 4; ++i) {
      parameters[i].ParameterType = i == 3 ? D3D12_ROOT_PARAMETER_TYPE_SRV
                                           : D3D12_ROOT_PARAMETER_TYPE_CBV;
      parameters[i].Descriptor.ShaderRegister = i == 2 ? 3 : i == 3 ? 0 : i;
      parameters[i].ShaderVisibility = i == 2
          ? D3D12_SHADER_VISIBILITY_ALL : D3D12_SHADER_VISIBILITY_VERTEX;
    }
    parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[4].Constants.ShaderRegister = 5;
    parameters[4].Constants.Num32BitValues = 4;
    parameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[5].Descriptor.ShaderRegister = 0;
    parameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_DESCRIPTOR_RANGE material_range{};
    material_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    material_range.NumDescriptors = 1;
    material_range.BaseShaderRegister = 1;
    parameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[6].DescriptorTable = {1, &material_range};
    parameters[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[7].Descriptor.ShaderRegister = 4;
    parameters[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[8].Descriptor.ShaderRegister = 1;
    parameters[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[9].Descriptor.ShaderRegister = 2;
    parameters[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_DESCRIPTOR_RANGE bindless_ranges[3]{};
    for (uint32_t i = 0; i < 3; ++i) {
      auto& range = bindless_ranges[i];
      range.RangeType = i == 0 ? D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
                               : D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
      range.NumDescriptors = UINT_MAX;
      range.RegisterSpace = i == 1 ? 1 : i == 2 ? 3 : 0;
      parameters[10 + i].ParameterType =
          D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameters[10 + i].DescriptorTable = {1, &range};
      parameters[10 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_ROOT_SIGNATURE_DESC description{
        13, parameters, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    if (FAILED(D3D12SerializeRootSignature(
            &description, D3D_ROOT_SIGNATURE_VERSION_1, &serialized,
            &errors)))
      return false;
    return SUCCEEDED(device->CreateRootSignature(
        0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
        IID_PPV_ARGS(&root)));
  }

  bool Pipeline(const rex::system::NativeGuestOutputRenderContext& context,
                const Snr04TrackDraw& draw) {
    const uint32_t material = TrackMaterialKind(draw);
    const PipelineKey key{draw.shader, draw.specialization, draw.raster_mode,
                          draw.clip_control, draw.depth_control, material};
    if (pipelines.contains(key)) return true;
    const uint8_t* vertex = nullptr;
    size_t vertex_size = 0;
    if (!context.shader ||
        !context.shader(context, 0, draw.shader, draw.specialization,
                        &vertex, &vertex_size) ||
        !vertex || vertex_size < 4 || std::memcmp(vertex, "DXBC", 4))
      return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature = root.Get();
    description.VS = {vertex, vertex_size};
    if (material == 3) {
      const uint8_t* pixel_bytecode = nullptr;
      size_t pixel_size = 0;
      if (!context.shader(context, 1, draw.pixel_shader,
                          draw.pixel_specialization, &pixel_bytecode,
                          &pixel_size) || !pixel_bytecode || pixel_size < 4 ||
          std::memcmp(pixel_bytecode, "DXBC", 4))
        return false;
      description.PS = {pixel_bytecode, pixel_size};
    } else {
      ID3DBlob* fragment = material == 1 ? pixel_textured.Get()
          : material == 2 ? pixel_road.Get() : pixel.Get();
      description.PS = {fragment->GetBufferPointer(), fragment->GetBufferSize()};
    }
    description.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    description.SampleMask = UINT_MAX;
    description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    description.RasterizerState.CullMode = (draw.raster_mode & 2)
        ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
    description.RasterizerState.FrontCounterClockwise =
        (draw.raster_mode & 4) == 0;
    description.RasterizerState.DepthClipEnable =
        (draw.clip_control & (1u << 16)) == 0;
    description.DepthStencilState.DepthEnable =
        (draw.depth_control & 2) != 0;
    description.DepthStencilState.DepthWriteMask = (draw.depth_control & 4)
        ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC(
        uint32_t(D3D12_COMPARISON_FUNC_NEVER) +
        ((draw.depth_control >> 4) & 7));
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;
    description.NumRenderTargets = 1;
    description.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
    description.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    description.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pipeline;
    const HRESULT pipeline_result = device->CreateGraphicsPipelineState(
        &description, IID_PPV_ARGS(&pipeline));
    if (FAILED(pipeline_result)) {
      REXGPU_INFO("FH1 native track pipeline rejected shader={:016X} "
                  "specialization={:X} material={} hresult={:08X}",
                  draw.shader, draw.specialization, material,
                  uint32_t(pipeline_result));
      return false;
    }
    pipelines.emplace(key, std::move(pipeline));
    return true;
  }

  bool ItemPipeline(const rex::system::NativeGuestOutputRenderContext& context,
                    uint64_t hash, uint64_t specialization,
                    bool alpha = false) {
    const auto key = std::tuple{hash, specialization, alpha};
    if (item_pipelines.contains(key)) return true;
    const uint8_t* vertex = nullptr;
    size_t size = 0;
    if (!context.shader ||
        !context.shader(context, 0, hash, specialization, &vertex, &size) ||
        !vertex || size < 4 || std::memcmp(vertex, "DXBC", 4))
      return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = root.Get();
    desc.VS = {vertex, size};
    ID3DBlob* fragment = alpha ? pixel_foliage.Get() : pixel.Get();
    desc.PS = {fragment->GetBufferPointer(), fragment->GetBufferSize()};
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.DepthStencilState.DepthEnable = TRUE;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pipeline;
    if (FAILED(device->CreateGraphicsPipelineState(&desc,
                                                  IID_PPV_ARGS(&pipeline))))
      return false;
    item_pipelines.emplace(key, std::move(pipeline));
    return true;
  }

  bool ManagerPipeline(
      const rex::system::NativeGuestOutputRenderContext& context,
      const Snr04ManagerScene& scene, bool original = false) {
    auto& pipeline = original ? manager_original_pipeline : manager_pipeline;
    if (pipeline) return true;
    const uint8_t* vertex = nullptr;
    size_t size = 0;
    if (!context.shader || !context.shader(context, 0,
            0xB8489164D5A86043ull, 31, &vertex, &size) ||
        !vertex || size < 4 || std::memcmp(vertex, "DXBC", 4))
      return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = root.Get();
    desc.VS = {vertex, size};
    if (original) {
      const uint8_t* fragment = nullptr;
      size_t fragment_size = 0;
      if (!context.shader(context, 1, 0x68150A8E959006CDull, 0x15001full,
                          &fragment, &fragment_size) || !fragment ||
          fragment_size < 4 || std::memcmp(fragment, "DXBC", 4))
        return false;
      desc.PS = {fragment, fragment_size};
    } else {
      desc.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
    }
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = (scene.raster_mode & 2)
        ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
    desc.RasterizerState.FrontCounterClockwise =
        (scene.raster_mode & 4) == 0;
    desc.RasterizerState.DepthClipEnable =
        (scene.clip_control & (1u << 16)) == 0;
    desc.DepthStencilState.DepthEnable = (scene.depth_control & 2) != 0;
    desc.DepthStencilState.DepthWriteMask = (scene.depth_control & 4)
        ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC(
        uint32_t(D3D12_COMPARISON_FUNC_NEVER) +
        ((scene.depth_control >> 4) & 7));
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    return SUCCEEDED(device->CreateGraphicsPipelineState(
        &desc, IID_PPV_ARGS(&pipeline)));
  }

  bool RemainderPipeline(
      const rex::system::NativeGuestOutputRenderContext& context,
      const Snr04RemainderDraw& draw) {
    const RemainderPipelineKey key{
        draw.shader, draw.specialization, draw.raster, draw.clip,
        draw.depth, draw.primitive, draw.format, draw.restart,
        RemainderMaterialKind(draw), uint32_t(draw.pixel_shader == 0)};
    if (remainder_pipelines.contains(key)) return true;
    const uint8_t* vertex = nullptr;
    size_t size = 0;
    if (!context.shader ||
        !context.shader(context, 0, draw.shader, draw.specialization,
                        &vertex, &size) ||
        !vertex || size < 4 || std::memcmp(vertex, "DXBC", 4))
      return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = root.Get();
    desc.VS = {vertex, size};
    const uint32_t material = RemainderMaterialKind(draw);
    if (material == 2 || material == 3 || material == 4) {
      const uint8_t* pixel_bytecode = nullptr;
      size_t pixel_size = 0;
      if (!context.shader(context, 1, draw.pixel_shader,
                          draw.pixel_specialization, &pixel_bytecode,
                          &pixel_size) || !pixel_bytecode || pixel_size < 4 ||
          std::memcmp(pixel_bytecode, "DXBC", 4))
        return false;
      desc.PS = {pixel_bytecode, pixel_size};
    } else {
      ID3DBlob* fragment = material == 1 ? pixel_car.Get() : pixel.Get();
      desc.PS = {fragment->GetBufferPointer(), fragment->GetBufferSize()};
    }
    if (material == 1) {
      auto& blend = desc.BlendState.RenderTarget[0];
      blend.BlendEnable = TRUE;
      blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
      blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
      blend.BlendOp = D3D12_BLEND_OP_ADD;
      blend.SrcBlendAlpha = D3D12_BLEND_ONE;
      blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
      blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        draw.pixel_shader ? D3D12_COLOR_WRITE_ENABLE_ALL : 0;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = (draw.raster & 2)
        ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
    desc.RasterizerState.FrontCounterClockwise = (draw.raster & 4) == 0;
    desc.RasterizerState.DepthClipEnable =
        (draw.clip & (1u << 16)) == 0;
    desc.DepthStencilState.DepthEnable = (draw.depth & 2) != 0;
    desc.DepthStencilState.DepthWriteMask = (draw.depth & 4)
        ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC(
        uint32_t(D3D12_COMPARISON_FUNC_NEVER) + ((draw.depth >> 4) & 7));
    desc.IBStripCutValue = !draw.restart
        ? D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED
        : draw.format ? D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF
                      : D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pipeline;
    if (FAILED(device->CreateGraphicsPipelineState(&desc,
                                                  IID_PPV_ARGS(&pipeline))))
      return false;
    if (material == 2)
      REXGPU_INFO("FH1 native original car pixel shader={:016X} "
                  "specialization={:X}", draw.pixel_shader,
                  draw.pixel_specialization);
    remainder_pipelines.emplace(key, std::move(pipeline));
    return true;
  }

  bool ProducerPipeline(
      const rex::system::NativeGuestOutputRenderContext& context,
      const OrderedUiDraw& draw, bool scene) {
    const ProducerPipelineKey key{draw.vertex_shader,
                                  draw.vertex_specialization,
                                  draw.pixel_shader,
                                  draw.pixel_specialization, draw.raster,
                                  draw.final_depth, draw.color_mask, scene};
    if (producer_pipelines.contains(key)) return true;
    const uint8_t *vertex = nullptr, *pixel_bytecode = nullptr;
    size_t vertex_size = 0, pixel_size = 0;
    if (!context.shader || !draw.pixel_shader ||
        !context.shader(context, 0, draw.vertex_shader,
                        draw.vertex_specialization, &vertex, &vertex_size) ||
        !context.shader(context, 1, draw.pixel_shader,
                        draw.pixel_specialization, &pixel_bytecode,
                        &pixel_size) || !vertex || !pixel_bytecode ||
        vertex_size < 4 || pixel_size < 4 ||
        std::memcmp(vertex, "DXBC", 4) ||
        std::memcmp(pixel_bytecode, "DXBC", 4)) return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = root.Get();
    desc.VS = {vertex, vertex_size};
    desc.PS = {pixel_bytecode, pixel_size};
    if (draw.primitive == 8) {
      if (!rectangle_geometry) {
        constexpr char shader[] =
            "struct V {float4 uv:TEXCOORD0; float4 p:SV_Position;};"
            "[maxvertexcount(4)] void main(triangle V v[3],"
            " inout TriangleStream<V> s) {"
            " V d; d.uv=v[1].uv+v[2].uv-v[0].uv;"
            " d.p=v[1].p+v[2].p-v[0].p;"
            " s.Append(v[0]); s.Append(v[1]); s.Append(v[2]);"
            " s.Append(d); }";
        ComPtr<ID3DBlob> errors;
        if (FAILED(D3DCompile(shader, sizeof(shader) - 1, nullptr,
                              nullptr, nullptr, "main", "gs_5_1", 0, 0,
                              &rectangle_geometry, &errors))) return false;
      }
      desc.GS = {rectangle_geometry->GetBufferPointer(),
                 rectangle_geometry->GetBufferSize()};
    }
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        uint8_t(draw.color_mask & 15);
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = (draw.raster & 2)
        ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
    desc.RasterizerState.FrontCounterClockwise = (draw.raster & 4) == 0;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.DepthStencilState.DepthEnable = (draw.final_depth & 2) != 0;
    desc.DepthStencilState.DepthWriteMask =
        scene && (draw.final_depth & 4)
            ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC(
        uint32_t(D3D12_COMPARISON_FUNC_NEVER) +
        ((draw.final_depth >> 4) & 7));
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = scene ? DXGI_FORMAT_R10G10B10A2_UNORM
                               : DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.DSVFormat = scene ? DXGI_FORMAT_D32_FLOAT
                           : DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    desc.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pipeline;
    if (FAILED(device->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&pipeline)))) return false;
    producer_pipelines.emplace(key, std::move(pipeline));
    return true;
  }

  bool BlitReady() {
    if (blit_pipeline && scene_blit_pipeline && seed_pipeline) return true;
    constexpr char vertex[] =
        "float4 main(uint id : SV_VertexID) : SV_Position {"
        " float2 p[3] = {float2(-1,-1),float2(-1,3),float2(3,-1)};"
        " return float4(p[id],0,1); }";
    constexpr char fragment[] =
        "Texture2D<float4> scene : register(t0);"
        "Texture2D<float4> guest : register(t1);"
        "float4 main(float4 p : SV_Position) : SV_Target0 {"
        " float2 q=p.xy;"
        " float4 s=scene.Load(int3(1279-int(q.x),719-int(q.y),0));"
        " float4 g=guest.Load(int3(int(q.x),int(q.y),0));"
        " float lo=min(g.r,min(g.g,g.b));"
        " float white=(lo>0.78)?1:0;"
        " float text=(q.x>55 && q.x<255 && q.y>22 && q.y<90)?white:0;"
        " text=max(text,(q.x>1015 && q.x<1240 && q.y>20 && q.y<90)?white:0);"
        " text=max(text,(q.x>1000 && q.y>=105 && q.y<230)?1:0);"
        " text=max(text,(q.x>60 && q.x<225 && q.y>107 && q.y<138)?1:0);"
        " float map=1-smoothstep(68,78,length(q-float2(135,540)));"
        " float speed=1-smoothstep(96,108,length(q-float2(1128,592)));"
        " return lerp(s,g,max(text,max(map,speed))); }";
    constexpr char scene_fragment[] =
        "Texture2D<float4> scene : register(t0);"
        "float4 main(float4 p : SV_Position) : SV_Target0 {"
        " return scene.Load(int3(1279-int(p.x),719-int(p.y),0)); }";
    constexpr char seed_fragment[] =
        "Texture2DArray<float4> source : register(t0);"
        "float4 main(float4 p : SV_Position) : SV_Target0 {"
        " return source.Load(int4(int(p.x),int(p.y),0,0)); }";
    ComPtr<ID3DBlob> vs, ps, scene_ps, seed_ps, errors, serialized;
    if (FAILED(D3DCompile(vertex, sizeof(vertex) - 1, nullptr, nullptr,
                          nullptr, "main", "vs_5_1", 0, 0, &vs, &errors)) ||
        FAILED(D3DCompile(fragment, sizeof(fragment) - 1, nullptr, nullptr,
                          nullptr, "main", "ps_5_1", 0, 0, &ps, &errors)) ||
        FAILED(D3DCompile(scene_fragment, sizeof(scene_fragment) - 1,
                          nullptr, nullptr, nullptr, "main", "ps_5_1", 0, 0,
                          &scene_ps, &errors)) ||
        FAILED(D3DCompile(seed_fragment, sizeof(seed_fragment) - 1,
                          nullptr, nullptr, nullptr, "main", "ps_5_1", 0, 0,
                          &seed_ps, &errors)))
      return false;
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 2;
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable = {1, &range};
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC root_desc{
        1, &parameter, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    if (FAILED(D3D12SerializeRootSignature(
            &root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
            &serialized, &errors)) ||
        FAILED(device->CreateRootSignature(
            0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
            IID_PPV_ARGS(&blit_root))))
      return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = blit_root.Get();
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
    desc.SampleDesc.Count = 1;
    if (FAILED(device->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&blit_pipeline))))
      return false;
    desc.PS = {scene_ps->GetBufferPointer(), scene_ps->GetBufferSize()};
    if (FAILED(device->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&scene_blit_pipeline)))) {
      blit_pipeline.Reset();
      blit_root.Reset();
      return false;
    }
    desc.PS = {seed_ps->GetBufferPointer(), seed_ps->GetBufferSize()};
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (FAILED(device->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(&seed_pipeline)))) return false;
    return true;
  }
};

bool PrepareItems(const rex::system::NativeGuestOutputRenderContext& context,
                  const Snr04ProceduralScene& scene, TrackGraphics& graphics,
                  UploadArena& arena, std::vector<ItemDrawBinding>& bindings,
                  uint64_t& index_offset, uint32_t& index_bytes) {
  if (scene.items.empty() || scene.items.size() > 512)
    return false;
  uint32_t max_vertices = 0;
  for (const auto& item : scene.items)
    for (const auto& draw : item.draws)
      max_vertices = (std::max)(max_vertices, draw.vertex_count);
  if (!max_vertices || max_vertices > UINT16_MAX || max_vertices % 4)
    return false;
  std::vector<uint16_t> indices;
  indices.reserve(max_vertices / 4 * 6);
  for (uint32_t first = 0; first < max_vertices; first += 4)
    for (uint32_t corner : {0u, 1u, 3u, 1u, 2u, 3u})
      indices.push_back(uint16_t(first + corner));
  index_bytes = uint32_t(indices.size() * sizeof(uint16_t));
  index_offset = arena.Add(indices.data(), index_bytes);
  for (const auto& item : scene.items) {
    if (item.vertices.empty() || item.constants.empty() ||
        item.draws.empty() || item.fetch[2] > 3)
      return false;
    const auto vertex = arena.Add(item.vertices.data(), item.vertices.size());
    const auto b1 = arena.Add(item.constants.data(),
                              item.constants.size() * sizeof(uint32_t));
    std::array<uint32_t, 192> fetch{};
    std::copy(item.fetch.begin(), item.fetch.end(), fetch.begin() + 188);
    const auto b3 = arena.Add(fetch.data(), sizeof(fetch));
    for (const auto& draw : item.draws) {
      const bool known_shader =
          (scene.character &&
           draw.vertex_shader == 0xAC345DADF2F24AE4ull) ||
          (!scene.character && (
          draw.vertex_shader == 0x3BC346726C1C2535ull ||
          draw.vertex_shader == 0xBDFD2AD68464101Aull ||
          draw.vertex_shader == 0xCB8AC98467C0C283ull ||
          draw.vertex_shader == 0xA715C815EDB8EEE8ull));
      const uint64_t specialization =
          draw.vertex_shader == 0xCB8AC98467C0C283ull ||
          draw.vertex_shader == 0xA715C815EDB8EEE8ull ? 127 : 15;
      if (!known_shader || !draw.sequence || !draw.vertex_count ||
          draw.vertex_count % 4 ||
          draw.vertex_count * (scene.character ? 4 : 10) != item.vertices.size() ||
          !graphics.ItemPipeline(context, draw.vertex_shader, specialization))
        return false;
      std::array<uint32_t, 120> system{};
      std::copy(draw.system.begin(), draw.system.end(), system.begin());
      if (!scene.character) {
        system[33] = std::bit_cast<uint32_t>(1.f);
        system[37] = std::bit_cast<uint32_t>(-1.f / 720.f);
      }
      bindings.push_back({draw.sequence, draw.vertex_shader, specialization,
                          draw.vertex_count / 4 * 6, vertex,
                          arena.Add(system.data(), sizeof(system)), b1, b3});
      if (scene.character) {
        auto& binding = bindings.back();
        const float tile_offset = 720.f - draw.viewport[3];
        binding.viewport = {draw.viewport[0], tile_offset,
                            draw.viewport[2], draw.viewport[3],
                            draw.viewport[4], draw.viewport[5]};
        binding.scissor = {draw.scissor[0],
                           LONG(draw.scissor[1] + tile_offset),
                           draw.scissor[2],
                           LONG(draw.scissor[3] + tile_offset)};
      }
    }
  }
  std::sort(bindings.begin(), bindings.end(),
            [](const auto& a, const auto& b) { return a.sequence < b.sequence; });
  for (size_t i = 1; i < bindings.size(); ++i)
    if (bindings[i - 1].sequence == bindings[i].sequence) return false;
  return !bindings.empty();
}

bool PrepareVegetation(
    const rex::system::NativeGuestOutputRenderContext& context,
    const Snr04VegetationScene& scene, TrackGraphics& graphics,
    UploadArena& arena, std::vector<ItemDrawBinding>& bindings,
    uint64_t& index_offset, uint32_t& index_bytes) {
  constexpr uint64_t shader = 0x5834939992FFC765ull;
  if (!scene.sequenced || scene.items.empty() || scene.items.size() > 512 ||
      !graphics.ItemPipeline(context, shader, 31))
    return false;
  uint32_t max_vertices = 0;
  for (const auto& item : scene.items)
    max_vertices = (std::max)(max_vertices, item.vertex_count);
  if (!max_vertices || max_vertices > UINT16_MAX || max_vertices % 4)
    return false;
  std::vector<uint16_t> indices;
  indices.reserve(max_vertices / 4 * 6);
  for (uint32_t first = 0; first < max_vertices; first += 4)
    for (uint32_t corner : {0u, 1u, 3u, 1u, 2u, 3u})
      indices.push_back(uint16_t(first + corner));
  index_bytes = uint32_t(indices.size() * sizeof(uint16_t));
  index_offset = arena.Add(indices.data(), index_bytes);
  for (const auto& item : scene.items) {
    if (!item.vertex_count || item.vertex_count % 4 ||
        item.vertices.size() != item.vertex_count * 4 ||
        item.variants.empty() || item.fetch[2] > 3)
      return false;
    const auto vertex = arena.Add(item.vertices.data(), item.vertices.size());
    std::array<uint32_t, 192> fetch{};
    std::copy(item.fetch.begin(), item.fetch.end(), fetch.begin() + 188);
    const auto b3 = arena.Add(fetch.data(), sizeof(fetch));
    for (const auto& variant : item.variants) {
      if (!variant.sequence) return false;
      std::array<uint32_t, 120> system{};
      std::copy(variant.system.begin(), variant.system.end(), system.begin());
      bindings.push_back({variant.sequence, shader, 31,
                          item.vertex_count / 4 * 6, vertex,
                          arena.Add(system.data(), sizeof(system)),
                          arena.Add(variant.vertex_constants.data(), 23 * 16),
                          b3});
      bindings.back().alpha = item.pixel_specialization == 0x1A001Full;
    }
  }
  std::sort(bindings.begin(), bindings.end(),
            [](const auto& a, const auto& b) { return a.sequence < b.sequence; });
  for (size_t i = 1; i < bindings.size(); ++i)
    if (bindings[i - 1].sequence == bindings[i].sequence) return false;
  return !bindings.empty();
}

bool PrepareRemainder(
    const rex::system::NativeGuestOutputRenderContext& context,
    const Snr04RemainderScene& scene, TrackGraphics& graphics,
    UploadArena& arena, std::vector<RemainderDrawBinding>& bindings,
    uint64_t& vertex_offset) {
  if (scene.vertex_bytes.empty() || scene.draws.empty() ||
      scene.host_indices.empty())
    return false;
  vertex_offset = arena.Add(scene.vertex_bytes.data(), scene.vertex_bytes.size());
  std::map<Snr04RemainderIndexKey, uint64_t> indices;
  for (const auto& [key, bytes] : scene.host_indices)
    indices.emplace(key, arena.Add(bytes.data(), bytes.size()));
  bindings.reserve(scene.draws.size());
  for (const auto& draw : scene.draws) {
    if (!graphics.RemainderPipeline(context, draw))
      throw std::runtime_error("unsupported remainder shader " +
                               std::to_string(draw.shader));
    const float tile_offset = 720.f - draw.viewport[3];
    if (tile_offset < 0 || tile_offset != std::floor(tile_offset) ||
        draw.viewport[0] != 0 || draw.viewport[1] != 0 ||
        draw.viewport[2] != 1280 || draw.scissor[0] != 0 ||
        draw.scissor[1] != 0 || draw.scissor[2] != 1280 ||
        draw.scissor[3] > draw.viewport[3] ||
        tile_offset + draw.scissor[3] > 720)
      return false;
    const Snr04RemainderIndexKey index_key{
        draw.index, draw.count, draw.index_type, draw.format,
        draw.endian, draw.reset_index};
    const auto& bytes = scene.host_indices.at(index_key);
    RemainderDrawBinding binding;
    binding.sequence = draw.sequence;
    binding.pipeline = {draw.shader, draw.specialization, draw.raster,
                        draw.clip, draw.depth, draw.primitive, draw.format,
                        draw.restart, RemainderMaterialKind(draw),
                        uint32_t(draw.pixel_shader == 0)};
    binding.index = indices.at(index_key);
    binding.index_bytes = uint32_t(bytes.size());
    binding.count = draw.count;
    binding.format = draw.format;
    binding.primitive = draw.primitive;
    binding.family = draw.family;
    binding.color = draw.family == 1
        ? std::array<float, 4>{0.65f, 0.16f, 0.12f, 1.f}
        : std::array<float, 4>{0.36f, 0.35f, 0.34f, 1.f};
    if (RemainderMaterialKind(draw) == 2) {
      const auto* blend = RemainderPixelConstant(draw, 47);
      const auto* multiplier = RemainderPixelConstant(draw, 157);
      const auto* alpha = RemainderPixelConstant(draw, 57);
      if (!blend || !multiplier || !alpha ||
          draw.system[61] != 0x3F800000u) return false;
      if (draw.pixel_packed.size() != 12) return false;
      binding.pixel_constants = arena.Add(draw.pixel_packed.data(),
                                          draw.pixel_packed.size() * 4);
    }
    const uint32_t shader_material = RemainderMaterialKind(draw);
    if (shader_material == 3 || shader_material == 4) {
      if (draw.system[61] != 0x3F800000u) return false;
      if (shader_material == 3) {
        if (draw.pixel_packed.size() < 46 * 4) return false;
        binding.pixel_constants = arena.Add(draw.pixel_packed.data(),
                                            draw.pixel_packed.size() * 4);
      } else {
        std::array<uint32_t, 256 * 4> constants{};
        uint32_t packed = 0;
        for (uint32_t reg = 0; reg < 256; ++reg)
          if (draw.pixel_bitmap[reg / 64] & (uint64_t(1) << (reg % 64))) {
            std::copy_n(draw.pixel_packed.data() + packed, 4,
                        constants.data() + reg * 4);
            packed += 4;
          }
        binding.pixel_constants = arena.Add(constants.data(), sizeof(constants));
      }
      std::array<uint32_t, 40> bools{};
      bools[4] = draw.bool_word4;
      binding.shader_bool = arena.Add(bools.data(), sizeof(bools));
      std::array<uint32_t, 28> descriptors{};
      const uint32_t count = shader_material == 3 ? 8 : 5;
      for (uint32_t i = 0; i < count; ++i) {
        const bool cube = shader_material == 3 ? i >= 6 : i == 2 || i == 3;
        const uint32_t view = shader_material == 3
            ? (cube ? i - 6 : i) : (cube ? i - 2 : i == 4 ? 2 : i);
        descriptors[i * 3 + 1] = i;
        descriptors[i * 3 + 2] = view * 2;
        descriptors[i * 3 + 3] = view * 2 + 1;
      }
      binding.b4 = arena.Add(descriptors.data(), sizeof(descriptors));
    }
    std::array<uint32_t, 120> system{};
    std::copy(draw.system.begin(), draw.system.end(), system.begin());
    binding.b0 = arena.Add(system.data(), sizeof(system));
    binding.b1 = arena.Add(draw.packed.data(),
                           draw.packed.size() * sizeof(uint32_t));
    binding.b3 = arena.Add(draw.bound_fetch.data(),
                           sizeof(draw.bound_fetch));
    if (RemainderMaterialKind(draw) == 1) {
      if (draw.pixel_specialization != 0x400000000003ull ||
          draw.texture_count != 1 || draw.system[61] != 0x3F800000u ||
          (draw.system[11] & 3u) != 0 ||
          (draw.bound_fetch[2] & 0x1FFFu) != 63 ||
          ((draw.bound_fetch[2] >> 13) & 0x1FFFu) != 63 ||
          ((draw.bound_fetch[3] >> 13) & 63u) != 0)
        return false;
      std::array<uint32_t, 20> constants{};
      for (uint32_t slot = 0; slot < 5; ++slot) {
        const uint32_t reg = slot == 4 ? 255 : slot;
        const auto* source = RemainderPixelConstant(draw, reg);
        if (!source) return false;
        std::copy_n(source, 4, constants.begin() + slot * 4);
      }
      const float width_x = std::bit_cast<float>(constants[4]);
      const float width_y = std::bit_cast<float>(constants[5]);
      if (!std::isfinite(width_x) || !std::isfinite(width_y) ||
          width_x <= 0 || width_y <= 0)
        return false;
      binding.b4 = arena.Add(constants.data(), sizeof(constants));
    }
    binding.viewport = {draw.viewport[0], tile_offset, draw.viewport[2],
                        draw.viewport[3], draw.viewport[4], draw.viewport[5]};
    binding.scissor = {draw.scissor[0], LONG(draw.scissor[1] + tile_offset),
                       draw.scissor[2], LONG(draw.scissor[3] + tile_offset)};
    bindings.push_back(binding);
  }
  return true;
}

bool CreateFrame(ID3D12Device* device, ID3D12Resource* output,
                 const UploadArena& arena, bool scene_only,
                 bool ordered_tiles, ID3D12Resource* initial_depth,
                 TrackFrame& frame) {
  D3D12_HEAP_PROPERTIES upload_heap{};
  upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC upload_desc{};
  upload_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  upload_desc.Width = arena.bytes.size();
  upload_desc.Height = 1;
  upload_desc.DepthOrArraySize = 1;
  upload_desc.MipLevels = 1;
  upload_desc.SampleDesc.Count = 1;
  upload_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (FAILED(device->CreateCommittedResource(
          &upload_heap, D3D12_HEAP_FLAG_NONE, &upload_desc,
          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
          IID_PPV_ARGS(&frame.upload))))
    return false;
  void* mapped = nullptr;
  if (FAILED(frame.upload->Map(0, nullptr, &mapped))) return false;
  std::memcpy(mapped, arena.bytes.data(), arena.bytes.size());
  frame.upload->Unmap(0, nullptr);

  D3D12_HEAP_PROPERTIES default_heap{};
  default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC color_desc{};
  color_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  color_desc.Width = 1280;
  color_desc.Height = 720;
  color_desc.DepthOrArraySize = 1;
  color_desc.MipLevels = 1;
  color_desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
  color_desc.SampleDesc.Count = 1;
  color_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  if (FAILED(device->CreateCommittedResource(
          &default_heap, D3D12_HEAP_FLAG_NONE, &color_desc,
          D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
          IID_PPV_ARGS(&frame.color))))
    return false;
  if (ordered_tiles && FAILED(device->CreateCommittedResource(
          &default_heap, D3D12_HEAP_FLAG_NONE, &color_desc,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
          IID_PPV_ARGS(&frame.color_tiles))))
    return false;
  if (ordered_tiles) {
    auto downsample_desc = color_desc;
    downsample_desc.Width = 320;
    downsample_desc.Height = 192;
    if (FAILED(device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &downsample_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
            IID_PPV_ARGS(&frame.downsample)))) return false;
    if (!initial_depth) return false;
    const auto source_desc = initial_depth->GetDesc();
    if (source_desc.Width != 1280 || source_desc.Height != 2048 ||
        source_desc.Format != DXGI_FORMAT_R32G8X24_TYPELESS ||
        source_desc.SampleDesc.Count != 1 ||
        !(source_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
      return false;
    frame.offscreen_depth = initial_depth;
    auto producer_desc = color_desc;
    producer_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (FAILED(device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &producer_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
            IID_PPV_ARGS(&frame.offscreen))))
      return false;
    for (auto& version : frame.initial_color_versions)
      if (FAILED(device->CreateCommittedResource(
              &default_heap, D3D12_HEAP_FLAG_NONE, &producer_desc,
              D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
              IID_PPV_ARGS(&version))))
        return false;
  }
  if (!scene_only) {
    color_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (FAILED(device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &color_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&frame.hud))))
      return false;
  }

  D3D12_HEAP_PROPERTIES depth_heap{};
  depth_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC depth_desc{};
  depth_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  depth_desc.Width = 1280;
  depth_desc.Height = 720;
  depth_desc.DepthOrArraySize = 1;
  depth_desc.MipLevels = 1;
  depth_desc.Format = DXGI_FORMAT_D32_FLOAT;
  depth_desc.SampleDesc.Count = 1;
  depth_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  D3D12_CLEAR_VALUE clear{};
  clear.Format = DXGI_FORMAT_D32_FLOAT;
  clear.DepthStencil.Depth = 0;
  if (FAILED(device->CreateCommittedResource(
          &depth_heap, D3D12_HEAP_FLAG_NONE, &depth_desc,
          D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
          IID_PPV_ARGS(&frame.depth))))
    return false;
  if (ordered_tiles)
    for (auto& version : frame.depth_versions)
      if (FAILED(device->CreateCommittedResource(
              &depth_heap, D3D12_HEAP_FLAG_NONE, &depth_desc,
              D3D12_RESOURCE_STATE_COPY_DEST, &clear,
              IID_PPV_ARGS(&version))))
        return false;

  D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
  heap_desc.NumDescriptors = ordered_tiles ? 4 : 2;
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  if (FAILED(device->CreateDescriptorHeap(&heap_desc,
                                          IID_PPV_ARGS(&frame.rtv))))
    return false;
  device->CreateRenderTargetView(
      frame.color.Get(), nullptr,
      frame.rtv->GetCPUDescriptorHandleForHeapStart());
  auto output_rtv = frame.rtv->GetCPUDescriptorHandleForHeapStart();
  output_rtv.ptr += device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  device->CreateRenderTargetView(output, nullptr, output_rtv);
  if (ordered_tiles) {
    output_rtv.ptr += device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    device->CreateRenderTargetView(frame.offscreen.Get(), nullptr, output_rtv);
    output_rtv.ptr += device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    device->CreateRenderTargetView(frame.downsample.Get(), nullptr, output_rtv);
  }
  heap_desc.NumDescriptors = ordered_tiles ? 2 : 1;
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  if (FAILED(device->CreateDescriptorHeap(&heap_desc,
                                          IID_PPV_ARGS(&frame.dsv))))
    return false;
  device->CreateDepthStencilView(
      frame.depth.Get(), nullptr,
      frame.dsv->GetCPUDescriptorHandleForHeapStart());
  if (ordered_tiles) {
    D3D12_DEPTH_STENCIL_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    view.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH |
                 D3D12_DSV_FLAG_READ_ONLY_STENCIL;
    auto handle = frame.dsv->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    device->CreateDepthStencilView(
        frame.offscreen_depth.Get(), &view, handle);
  }
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  heap_desc.NumDescriptors = ordered_tiles ? 5 : 2;
  if (FAILED(device->CreateDescriptorHeap(&heap_desc,
                                          IID_PPV_ARGS(&frame.srv))))
    return false;
  D3D12_SHADER_RESOURCE_VIEW_DESC view{};
  view.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
  view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  view.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(
      frame.color.Get(), &view,
      frame.srv->GetCPUDescriptorHandleForHeapStart());
  auto hud_view = frame.srv->GetCPUDescriptorHandleForHeapStart();
  hud_view.ptr += device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  if (!scene_only) device->CreateShaderResourceView(frame.hud.Get(), &view, hud_view);
  if (ordered_tiles) {
    const auto stride = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    hud_view.ptr += stride;
    device->CreateShaderResourceView(frame.color_tiles.Get(), &view, hud_view);
    hud_view.ptr += stride;
    device->CreateShaderResourceView(frame.color_tiles.Get(), &view, hud_view);
    hud_view.ptr += stride;
    device->CreateShaderResourceView(frame.downsample.Get(), &view, hud_view);
  }
  return true;
}

bool DrawTrack(const rex::system::NativeGuestOutputRenderContext& context,
               const Snr04LiveScene& live, TrackGraphics& graphics,
               bool diagnostic_scene_only) {
  const auto reject = [&](const char* stage) {
    static const bool trace =
        rex::cvar::GetFlagByName("perf_critical_path_trace") == "true";
    if (trace || diagnostic_scene_only)
      REXGPU_WARN("FH1 native draw rejected source_frame={} stage={}",
                  live.source_frame, stage);
    return false;
  };
  const bool pre_ui =
      context.phase == rex::system::NativeGuestOutputPhase::kBeforeUi;
  const bool scene_only = pre_ui || diagnostic_scene_only;
  auto* device = static_cast<ID3D12Device*>(context.device);
  auto* output = static_cast<ID3D12Resource*>(context.guest_output);
  auto* list = static_cast<rex::graphics::d3d12::DeferredCommandList*>(
      context.deferred_command_list);
  if (!live.remainder) return reject("missing_remainder");
  if (!device || !output || !list || !live.track || !live.items ||
      !live.vegetation ||
      output->GetDesc().Format != DXGI_FORMAT_R10G10B10A2_UNORM ||
      output->GetDesc().Width != 1280 ||
      output->GetDesc().Height < 720 ||
      (pre_ui && context.guest_output_state !=
                     D3D12_RESOURCE_STATE_RENDER_TARGET) ||
      !graphics.Ready(device) || !graphics.BlitReady())
    return reject("input_or_pipeline");
  auto scene = ParseSnr04TrackScene(*live.track);
  auto remainder = ParseSnr04RemainderScene(*live.remainder);
  Snr04ProceduralScene characters;
  if (live.characters)
    characters = ParseSnr04CharacterScene(*live.characters);
  Snr04ManagerScene manager;
  if (live.manager)
    manager = ParseSnr04ManagerScene(*live.manager);
  if (scene.source_frame != live.source_frame || !scene.raster_captured ||
      scene.draws.empty() || live.items->frame != live.source_frame ||
      live.vegetation->source_frame != live.source_frame ||
      remainder.source_frame != live.source_frame ||
      (live.characters && characters.frame != live.source_frame) ||
      (live.manager && manager.source_frame != live.source_frame))
    return reject("scene_parse");
  while (!graphics.submitted.empty() &&
         graphics.submitted.front().first <= context.completed_submission)
    graphics.submitted.pop_front();
  if (graphics.submitted.size() >= 8) return reject("in_flight_limit");

  UploadArena arena;
  struct Material {
    ComPtr<ID3D12Resource> resource;
    ComPtr<ID3D12Resource> snapshot;
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    uint64_t allocation_id = 0, payload_generation = 0;
    bool pinned = false;
  };
  std::vector<Material> materials;
  std::map<std::tuple<std::array<uint32_t, 6>, uint64_t, uint64_t>,
           uint32_t> material_indices;
  const auto resolve_material = [&](const Snr04TrackTextureIdentity& identity,
                                    uint32_t& index,
                                    bool require_pinned_2d = false) {
    if (!context.texture || !identity.allocation_id ||
        !identity.payload_generation || identity.outdated_mask)
      return false;
    const auto key = std::tuple{identity.fetch_words, identity.allocation_id,
                                identity.payload_generation};
    if (const auto existing = material_indices.find(key);
        existing != material_indices.end()) {
      index = existing->second;
      return !require_pinned_2d ||
          (materials[index].pinned && materials[index].view.ViewDimension ==
              D3D12_SRV_DIMENSION_TEXTURE2D);
    }
    void* resource = nullptr;
    Material material;
    if (!context.texture(context, identity.fetch_words.data(),
                         identity.allocation_id, identity.payload_generation,
                         &resource, &material.view, &material.pinned) || !resource)
      return false;
    material.resource = static_cast<ID3D12Resource*>(resource);
    if (require_pinned_2d &&
        (!material.pinned || material.view.ViewDimension !=
            D3D12_SRV_DIMENSION_TEXTURE2D))
      return false;
    for (const auto& prior : materials)
      if (prior.resource.Get() == material.resource.Get() &&
          (prior.allocation_id != identity.allocation_id ||
           prior.payload_generation != identity.payload_generation))
        return false;
    material.allocation_id = identity.allocation_id;
    material.payload_generation = identity.payload_generation;
    index = uint32_t(materials.size());
    material_indices.emplace(key, index);
    materials.push_back(std::move(material));
    return true;
  };
  std::map<std::pair<uint64_t, uint32_t>,
           const Snr04TrackTextureIdentity*> texture_identities;
  if (live.track_textures) {
    for (const auto& identity : *live.track_textures)
      if (!texture_identities.emplace(
              std::pair{identity.sequence, identity.fetch_constant},
              &identity).second)
        return reject("duplicate_track_texture");
  }
  std::map<Snr04TrackRange, uint64_t> vertices, indices;
  for (const auto& [range, bytes] : scene.vertices)
    vertices.emplace(range, arena.Add(bytes.data(), bytes.size()));
  for (const auto& [range, bytes] : scene.indices)
    indices.emplace(range, arena.Add(bytes.data(), bytes.size()));
  std::vector<TrackDrawBinding> bindings;
  bindings.reserve(scene.draws.size());
  for (const auto& draw : scene.draws) {
    if (!graphics.Pipeline(context, draw)) {
      REXGPU_INFO("FH1 native track shader unavailable frame={} sequence={} "
                  "shader={:016X}", live.source_frame, draw.sequence,
                  draw.shader);
      return reject("track_pipeline");
    }
    const float tile_offset = 720.f - draw.viewport[3];
    if (tile_offset < 0 || tile_offset != std::floor(tile_offset) ||
        draw.viewport[0] != 0 || draw.viewport[1] != 0 ||
        draw.viewport[2] != 1280 || draw.scissor[0] != 0 ||
        draw.scissor[1] != 0 || draw.scissor[2] != 1280 ||
        draw.scissor[3] > draw.viewport[3] ||
        tile_offset + draw.scissor[3] > 720)
      return reject("track_viewport");
    TrackDrawBinding binding;
    const uint32_t material = TrackMaterialKind(draw);
    if (material == 1 || material == 2) {
      const auto found = texture_identities.find({draw.sequence, 0});
      if (found == texture_identities.end() ||
          !resolve_material(*found->second, binding.material_index)) {
        REXGPU_INFO("FH1 native track texture unavailable frame={} sequence={}",
                    live.source_frame, draw.sequence);
        return reject("track_texture");
      }
    }
    if (material == 3) {
      binding.shader_texture_count = uint32_t(draw.textures.size());
      const auto fetches = TrackShaderFetches(draw);
      for (uint32_t j = 0; j < draw.textures.size(); ++j) {
        const auto found = texture_identities.find({draw.sequence, fetches[j]});
        if (found == texture_identities.end() ||
            !resolve_material(*found->second, binding.shader_materials[j]) ||
            !materials[binding.shader_materials[j]].pinned ||
            materials[binding.shader_materials[j]].view.ViewDimension !=
                D3D12_SRV_DIMENSION_TEXTURE2D)
          return reject("track_structure_texture");
      }
      if (draw.pixel_packed.size() < 10 * 4) return reject("track_structure_constants");
      binding.pixel_constants = arena.Add(draw.pixel_packed.data(),
                                          draw.pixel_packed.size() * 4);
      std::array<uint32_t, 40> bools{};
      bools[7] = draw.bool_word7;
      binding.pixel_bool = arena.Add(bools.data(), sizeof(bools));
      std::array<uint32_t, 20> descriptors{};
      for (uint32_t j = 0; j < draw.textures.size(); ++j) {
        descriptors[j * 3 + 1] = j;
        descriptors[j * 3 + 2] = j * 2;
        descriptors[j * 3 + 3] = j * 2 + 1;
      }
      binding.pixel_descriptors = arena.Add(descriptors.data(),
                                            sizeof(descriptors));
    }
    binding.pipeline = {draw.shader, draw.specialization, draw.raster_mode,
                        draw.clip_control, draw.depth_control, material};
    binding.vertex = vertices.at(draw.vertex);
    binding.index = indices.at(draw.index);
    std::array<uint32_t, 120> system{};
    std::copy(draw.system.begin(), draw.system.end(), system.begin());
    std::array<uint32_t, 192> fetch{};
    std::copy(draw.fetch.begin(), draw.fetch.end(), fetch.begin() + 188);
    fetch[190] &= 3;
    if (material == 3) {
      const auto fetches = TrackShaderFetches(draw);
      for (uint32_t j = 0; j < draw.textures.size(); ++j) {
        const uint32_t texture = fetches[j];
        const auto* identity = texture_identities.at({draw.sequence, texture});
        std::copy(identity->fetch_words.begin(), identity->fetch_words.end(),
                  fetch.begin() + texture * 6);
      }
    }
    binding.b0 = arena.Add(system.data(), sizeof(system));
    binding.b1 = arena.Add(draw.packed.data(),
                           draw.packed.size() * sizeof(uint32_t));
    binding.b3 = arena.Add(fetch.data(), sizeof(fetch));
    binding.viewport = {draw.viewport[0], draw.viewport[1] + tile_offset,
                        draw.viewport[2], draw.viewport[3],
                        draw.viewport[4], draw.viewport[5]};
    binding.scissor = {draw.scissor[0],
                       LONG(draw.scissor[1] + tile_offset),
                       draw.scissor[2],
                       LONG(draw.scissor[3] + tile_offset)};
    bindings.push_back(binding);
  }
  std::vector<ItemDrawBinding> item_bindings;
  uint64_t item_index = 0;
  uint32_t item_index_bytes = 0;
  if (!PrepareItems(context, *live.items, graphics, arena, item_bindings,
                    item_index, item_index_bytes))
    return reject("prepare_items");
  std::vector<ItemDrawBinding> character_bindings;
  uint64_t character_index = 0;
  uint32_t character_index_bytes = 0;
  if (live.characters &&
      !PrepareItems(context, characters, graphics, arena,
                    character_bindings, character_index,
                    character_index_bytes))
    return reject("prepare_characters");
  std::vector<ManagerDrawBinding> manager_bindings;
  uint64_t manager_vertex = 0;
  if (live.manager) {
    if (!graphics.ManagerPipeline(context, manager))
      return reject("manager_pipeline");
    const bool have_materials = live.manager_materials &&
        live.manager_materials->size() == manager.draws.size() &&
        graphics.ManagerPipeline(context, manager, true);
    manager_vertex = arena.Add(manager.vertex_span.data(),
                               manager.vertex_span.size());
    std::map<Snr04ManagerRange, uint64_t> manager_indices;
    for (const auto& [range, bytes] : manager.indices)
      manager_indices.emplace(range, arena.Add(bytes.data(), bytes.size()));
    manager_bindings.reserve(manager.draws.size());
    for (size_t i = 0; i < manager.draws.size(); ++i) {
      const auto& draw = manager.draws[i];
      std::array<uint32_t, 120> system{};
      std::copy(draw.system.begin(), draw.system.end(), system.begin());
      std::array<uint32_t, 192> fetch{};
      std::copy(draw.fetch.begin(), draw.fetch.end(), fetch.begin() + 188);
      const float tile_offset = 720.f - draw.viewport[3];
      ManagerDrawBinding binding;
      binding.sequence = draw.sequence;
      binding.count = draw.count;
      binding.index_bytes = draw.ranges[2].second;
      binding.index = manager_indices.at(draw.ranges[2]);
      binding.b0 = arena.Add(system.data(), sizeof(system));
      binding.b1 = arena.Add(draw.packed.data(), draw.packed.size() * 4);
      binding.b3 = arena.Add(fetch.data(), sizeof(fetch));
      binding.viewport = {draw.viewport[0], tile_offset,
                          draw.viewport[2], draw.viewport[3],
                          draw.viewport[4], draw.viewport[5]};
      binding.scissor = {draw.scissor[0], LONG(draw.scissor[1] + tile_offset),
                         draw.scissor[2], LONG(draw.scissor[3] + tile_offset)};
      if (have_materials) {
        const auto& material = (*live.manager_materials)[i];
        if (material.sequence == draw.sequence &&
            material.pixel_specialization == 0x15001full &&
            material.pixel_packed.size() == 28 &&
            material.textures[0].fetch_constant == 0 &&
            material.textures[1].fetch_constant == 13) {
          std::array<uint32_t, 2> texture_indices{};
          bool supported = true;
          for (uint32_t j = 0; j < 2; ++j) {
            if (!resolve_material(material.textures[j], texture_indices[j],
                                  true)) {
              supported = false;
              break;
            }
          }
          if (supported) {
            binding.shader_materials = texture_indices;
            binding.pixel_constants = arena.Add(material.pixel_packed.data(),
                                                material.pixel_packed.size() * 4);
            binding.pixel_bool = arena.Add(material.bool_loop.data(),
                                           sizeof(material.bool_loop));
            // Shader fetch 13 uses descriptor triplet 0; fetch 0 uses 1.
            const std::array<uint32_t, 8> descriptors{0, 1, 2, 3,
                                                       4, 5, 6, 0};
            binding.b4 = arena.Add(descriptors.data(), sizeof(descriptors));
          }
        }
      }
      manager_bindings.push_back(binding);
    }
  }
  std::vector<ItemDrawBinding> vegetation_bindings;
  uint64_t vegetation_index = 0;
  uint32_t vegetation_index_bytes = 0;
  if (!PrepareVegetation(context, *live.vegetation, graphics, arena,
                         vegetation_bindings, vegetation_index,
                         vegetation_index_bytes))
    return reject("prepare_vegetation");
  std::map<std::pair<uint64_t, uint32_t>,
           const Snr04TrackTextureIdentity*> vegetation_textures;
  if (live.vegetation_textures)
    for (const auto& identity : *live.vegetation_textures)
      if (!vegetation_textures.emplace(
              std::pair{identity.sequence, identity.fetch_constant},
              &identity).second)
        return reject("duplicate_remainder_texture");
  for (auto& binding : vegetation_bindings) {
    if (!binding.alpha) continue;
    const auto alpha = vegetation_textures.find({binding.sequence, 0});
    if (alpha == vegetation_textures.end() ||
        !vegetation_textures.contains({binding.sequence, 13}) ||
        !graphics.ItemPipeline(context, binding.shader,
                               binding.specialization, true) ||
        !resolve_material(*alpha->second, binding.material_index)) {
      REXGPU_INFO("FH1 native foliage alpha unavailable frame={} sequence={}",
                  live.source_frame, binding.sequence);
      return reject("foliage_alpha");
    }
  }
  std::vector<RemainderDrawBinding> remainder_bindings;
  uint64_t remainder_vertex = 0;
  if (!PrepareRemainder(context, remainder, graphics, arena,
                        remainder_bindings, remainder_vertex))
    return reject("prepare_remainder");
  std::map<std::pair<uint64_t, uint32_t>,
           const Snr04TrackTextureIdentity*> remainder_textures;
  if (live.remainder_textures)
    for (const auto& identity : *live.remainder_textures)
      if (!remainder_textures.emplace(
              std::pair{identity.sequence, identity.fetch_constant},
              &identity).second)
        return false;
  for (size_t i = 0; i < remainder.draws.size(); ++i) {
    const uint32_t material = RemainderMaterialKind(remainder.draws[i]);
    if (material != 1 && material != 3 && material != 4) continue;
    if (material == 3 || material == 4) {
      constexpr uint32_t body_fetches[]{1, 0, 13, 2, 3, 4, 5, 6};
      constexpr uint32_t glass_fetches[]{13, 1, 2, 3, 4};
      const uint32_t count = material == 3 ? 8 : 5;
      for (uint32_t j = 0; j < count; ++j) {
        const uint32_t fetch = material == 3
            ? body_fetches[j] : glass_fetches[j];
        const auto input = remainder_textures.find({remainder.draws[i].sequence, fetch});
        if (input == remainder_textures.end() || !input->second->allocation_id ||
            !input->second->payload_generation || input->second->outdated_mask ||
            !resolve_material(*input->second,
                              remainder_bindings[i].shader_materials[j]))
          return reject("missing_car_texture");
        const auto& texture = materials[remainder_bindings[i].shader_materials[j]];
        const bool cube = material == 3 ? j >= 6 : j == 2 || j == 3;
        if (!texture.pinned ||
            texture.view.ViewDimension != (cube
                ? D3D12_SRV_DIMENSION_TEXTURECUBE
                : D3D12_SRV_DIMENSION_TEXTURE2D))
          return reject("unsupported_car_texture");
      }
      continue;
    }
    const auto found = remainder_textures.find({remainder.draws[i].sequence,
                                                0u});
    if (found == remainder_textures.end() ||
        !resolve_material(*found->second,
                          remainder_bindings[i].material_index))
      return reject("remainder_texture");
  }
  const auto trace_frame = std::strtoull(
      rex::cvar::GetFlagByName("pinyon_shift_snr01_trace_source_frame").c_str(),
      nullptr, 10);
  const bool ordered_tiles = trace_frame && live.source_frame + 1 == trace_frame;
  const bool downsample_probe = ordered_tiles &&
      REXCVAR_GET(pinyon_shift_native_downsample_probe);
  if (downsample_probe &&
      rex::cvar::GetFlagByName("pinyon_shift_native_race") == "true")
    return reject("downsample_probe_requires_shadow");
  std::optional<std::vector<OrderedFrameOperation>> ordered_operations;
  std::vector<ProducerDrawBinding> producer_bindings;
  uint64_t producer_clear_sequence = 0;
  uint32_t guest_second_color_index = UINT32_MAX;
  if (ordered_tiles) {
    ordered_operations = SnapshotOrderedFrameOperations(trace_frame);
    if (!ordered_operations) return reject("ordered_stream");
    const auto first_copy = std::find_if(
        ordered_operations->begin(), ordered_operations->end(),
        [](const auto& event) {
          return event.kind == 'C' && event.dest_base == 484626432 &&
              event.copy.source_base_tiles == 720;
        });
    if (first_copy == ordered_operations->end())
      return reject("ordered_initial_color_missing");
    const auto clear = std::find_if(
        std::make_reverse_iterator(first_copy), ordered_operations->rend(),
        [](const auto& event) {
          return event.kind == 'K' && event.color == 720;
        });
    const auto second_copy = std::find_if(
        std::next(first_copy), ordered_operations->end(),
        [](const auto& event) {
          return event.kind == 'C' && event.dest_base == 484626432 &&
              event.copy.source_base_tiles == 720;
        });
    if (clear == ordered_operations->rend() ||
        second_copy == ordered_operations->end())
      return reject("ordered_initial_producer_range");
    producer_clear_sequence = clear->sequence;
    auto prepare_producer = [&](uint64_t sequence, const OrderedUiDraw& draw,
                                bool scene, bool downsample = false) {
        const bool feedback = !scene && sequence > first_copy->sequence;
        if (draw.primitive != (downsample ? 8u : 13u) ||
            draw.vertices.size() != 1 ||
            draw.index_count != (downsample ? 24u :
                                 feedback || scene ? 4u : 24u) ||
            draw.index_type != (feedback || scene ? 0u : 1u) ||
            draw.textures.size() != (downsample ? 1u :
                                    feedback || scene ? 1u : 2u) ||
            draw.vertices[0].constant >= 96 ||
            draw.vertices[0].bytes.empty() ||
            draw.viewport[0] != 0 || draw.viewport[1] != 0 ||
            draw.viewport[2] != (downsample ? 320 : 1280) ||
            (downsample ? draw.viewport[3] != 192 :
             scene ? (draw.viewport[3] != 720 &&
                      draw.viewport[3] != 464 && draw.viewport[3] != 208)
                   : draw.viewport[3] != 720) ||
            draw.scissor != (downsample
                ? std::array<int32_t, 4>{0, 0, 320, 192}
                : std::array<int32_t, 4>{
                    0, 0, 1280,
                    scene ? int32_t(std::min(256.f, draw.viewport[3]))
                          : 720}) ||
            (downsample &&
                (draw.textures[0].fetch_constant != 0 ||
                 (draw.textures[0].fetch_words[1] >> 12) !=
                     (474877952u >> 12))) ||
            !graphics.ProducerPipeline(context, draw, scene)) return false;
        ProducerDrawBinding binding;
        binding.sequence = sequence;
        binding.pipeline = {draw.vertex_shader, draw.vertex_specialization,
                            draw.pixel_shader, draw.pixel_specialization,
                            draw.raster, draw.final_depth,
                            draw.color_mask, scene};
        binding.feedback = feedback;
        binding.indexed = !feedback && !scene;
        binding.downsample = downsample;
        binding.count = draw.index_count;
        binding.vertex = arena.Add(draw.vertices[0].bytes.data(),
                                   draw.vertices[0].bytes.size());
        if (binding.indexed) {
          if (draw.indices.size() != draw.index_length ||
              draw.index_endianness != 1 ||
              draw.index_length != draw.index_count * 2) return false;
          std::vector<uint16_t> indices(draw.index_count);
          for (uint32_t i = 0; i < draw.index_count; ++i)
            indices[i] = (uint16_t(draw.indices[i * 2]) << 8) |
                         draw.indices[i * 2 + 1];
          binding.index = arena.Add(indices.data(), indices.size() * 2);
        }
        std::array<uint32_t, 120> system{};
        std::copy(draw.system.begin(), draw.system.end(), system.begin());
        binding.b0 = arena.Add(system.data(), sizeof(system));
        std::array<uint32_t, 1024> vertex_constants{}, pixel_constants{};
        uint32_t vertex_words = 0, pixel_words = 0;
        for (uint32_t reg = 0; reg < 256; ++reg) {
          if (draw.vertex_bitmap[reg / 64] &
              (uint64_t(1) << (reg % 64))) {
            std::copy_n(draw.constants.data() + reg * 4, 4,
                        vertex_constants.data() + vertex_words);
            vertex_words += 4;
          }
          if (draw.pixel_bitmap[reg / 64] &
              (uint64_t(1) << (reg % 64))) {
            std::copy_n(draw.constants.data() + (256 + reg) * 4, 4,
                        pixel_constants.data() + pixel_words);
            pixel_words += 4;
          }
        }
        binding.b1 = arena.Add(vertex_constants.data(),
                               sizeof(vertex_constants));
        binding.pixel_constants = arena.Add(pixel_constants.data(),
                                            sizeof(pixel_constants));
        auto fetch = draw.fetch_constants;
        const uint32_t slot = draw.vertices[0].constant * 2;
        if ((fetch[slot] & 0x1FFFFFFC) !=
                (draw.vertices[0].base & 0x1FFFFFFC) ||
            (fetch[slot + 1] & 0x03FFFFFC) != draw.vertices[0].length)
          return false;
        fetch[slot] &= 3;
        binding.b3 = arena.Add(fetch.data(), sizeof(fetch));
        binding.bool_loop = arena.Add(draw.bool_loop.data(),
                                      sizeof(draw.bool_loop));
        std::array<uint32_t, 12> descriptors{};
        for (uint32_t j = 0; j < draw.textures.size(); ++j) {
          const auto& texture = draw.textures[j];
          Snr04TrackTextureIdentity identity;
          identity.sequence = sequence;
          identity.fetch_constant = texture.fetch_constant;
          std::copy_n(texture.fetch_words, 6,
                      identity.fetch_words.begin());
          identity.allocation_id = texture.allocation_id;
          identity.payload_generation = texture.payload_generation;
          identity.outdated_mask = texture.outdated_mask;
          if (!resolve_material(identity, binding.materials[j], true))
            return false;
          descriptors[j * 3 + 1] = j;
          descriptors[j * 3 + 2] = j * 2;
          descriptors[j * 3 + 3] = j * 2 + 1;
        }
        binding.material_count = uint32_t(draw.textures.size());
        binding.descriptors = arena.Add(descriptors.data(),
                                        sizeof(descriptors));
        const float tile_offset = scene && !downsample
            ? 720.f - draw.viewport[3] : 0.f;
        binding.viewport = {0, tile_offset,
                            downsample ? 320.f : 1280.f, draw.viewport[3],
                            draw.viewport[4], draw.viewport[5]};
        binding.scissor = {0, LONG(tile_offset),
                           downsample ? 320 : 1280,
                           LONG(tile_offset + draw.scissor[3])};
        producer_bindings.push_back(binding);
        return true;
    };
    if (!WithOrderedFrameDraws(trace_frame, clear->sequence + 1,
                              second_copy->sequence - 1,
                              [&](const auto& draws) {
      for (const auto& [sequence, draw] : draws) {
        if (draw.color != 720 || !draw.pixel_shader) continue;
        if (!prepare_producer(sequence, draw, false)) return false;
      }
      return producer_bindings.size() == 12 &&
          producer_bindings[11].feedback &&
          std::count_if(producer_bindings.begin(), producer_bindings.end(),
                        [](const auto& binding) {
                          return binding.feedback;
                        }) == 1;
    })) return reject("prepare_initial_producer");
    for (const auto& event : *ordered_operations) {
      if (event.kind != 'D' || event.surface != 335676672 ||
          event.color != 786432 ||
          event.vertex_shader != 0x21FBB5F33759B350ull ||
          event.pixel_shader != 0xCF453BD52292E8E8ull) continue;
      if (!WithOrderedFrameDraws(trace_frame, event.sequence, event.sequence,
                                [&](const auto& draws) {
        return draws.size() == 1 &&
            prepare_producer(event.sequence, draws.begin()->second, true);
      })) return reject("prepare_scene_producer");
    }
    if (producer_bindings.size() != 15)
      return reject("scene_producer_count");
    const auto downsample_event = std::find_if(
        ordered_operations->begin(), ordered_operations->end(),
        [](const auto& event) {
          return event.kind == 'D' && event.surface == 335545600 &&
              event.color == 196608 &&
              event.vertex_shader == 0x2C53E1A563484076ull &&
              event.pixel_shader == 0xE17BECBE8BE65806ull;
        });
    if (downsample_event == ordered_operations->end() ||
        !WithOrderedFrameDraws(trace_frame, downsample_event->sequence,
                               downsample_event->sequence,
                               [&](const auto& draws) {
          return draws.size() == 1 && prepare_producer(
              downsample_event->sequence, draws.begin()->second,
              true, true);
        }) || producer_bindings.size() != 16)
      return reject("prepare_downsample");
    // Fetch 13 in the first car-body draw reads the second resolved color
    // version. Identify its pinned texture before replacing one consumer.
    const auto guest_consumer = std::find_if(
        std::next(second_copy), ordered_operations->end(),
        [](const auto& event) {
          return event.kind == 'D' && event.color == 786432 &&
              event.pixel_shader == 16847216741118496823ull;
        });
    if (guest_consumer == ordered_operations->end() ||
        !WithOrderedFrameDraws(trace_frame, guest_consumer->sequence,
                               guest_consumer->sequence,
                               [&](const auto& draws) {
          if (draws.size() != 1) return false;
          for (const auto& texture : draws.begin()->second.textures) {
            if (texture.fetch_constant != 13 ||
                (texture.fetch_words[1] >> 12) != (484626432u >> 12))
              continue;
            Snr04TrackTextureIdentity identity;
            std::copy_n(texture.fetch_words, 6,
                        identity.fetch_words.begin());
            identity.allocation_id = texture.allocation_id;
            identity.payload_generation = texture.payload_generation;
            identity.outdated_mask = texture.outdated_mask;
            return resolve_material(identity, guest_second_color_index, true);
          }
          return false;
        })) return reject("guest_second_color");
  }
  TrackFrame frame;
  if (!CreateFrame(device, output, arena, scene_only, ordered_tiles,
                   static_cast<ID3D12Resource*>(
                       context.fh1_initial_color_depth), frame))
    return reject("create_frame");
  uint32_t original_count = 0;
  for (auto& binding : bindings)
    if (std::get<5>(binding.pipeline) == 3) {
      binding.shader_view_offset = uint32_t(materials.size()) + original_count * 16;
      binding.shader_sampler_offset = 2;
      ++original_count;
    }
  for (auto& binding : remainder_bindings)
    if (std::get<8>(binding.pipeline) == 3 ||
        std::get<8>(binding.pipeline) == 4) {
      binding.shader_view_offset = uint32_t(materials.size()) + original_count * 16;
      binding.shader_sampler_offset = std::get<8>(binding.pipeline) == 3
          ? 34 : 66;
      ++original_count;
    }
  for (auto& binding : manager_bindings)
    if (binding.pixel_constants) {
      binding.shader_view_offset = uint32_t(materials.size()) +
          original_count * 16;
      ++original_count;
    }
  for (auto& binding : producer_bindings) {
    binding.view_offset = uint32_t(materials.size()) + original_count * 16;
    ++original_count;
  }
  if (!materials.empty()) {
    D3D12_HEAP_PROPERTIES heap_properties{};
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    uint64_t snapshot_bytes = 0;
    for (auto& material : materials) {
      if (material.pinned) {
        material.snapshot = material.resource;
        continue;
      }
      const auto desc = material.resource->GetDesc();
      if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
          desc.DepthOrArraySize != 1 || desc.SampleDesc.Count != 1)
        return false;
      const uint64_t bytes =
          device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
      if (bytes > 64 * 1024 * 1024 - snapshot_bytes ||
          FAILED(device->CreateCommittedResource(
              &heap_properties, D3D12_HEAP_FLAG_NONE, &desc,
              D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
              IID_PPV_ARGS(&material.snapshot))))
        return false;
      snapshot_bytes += bytes;
    }
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = UINT(materials.size() + original_count * 16);
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(
            &heap, IID_PPV_ARGS(&frame.materials))))
      return false;
    auto handle = frame.materials->GetCPUDescriptorHandleForHeapStart();
    const auto stride = device->GetDescriptorHandleIncrementSize(heap.Type);
    for (auto& material : materials) {
      device->CreateShaderResourceView(material.snapshot.Get(),
                                       &material.view, handle);
      frame.material_resources.push_back(std::move(material.resource));
      frame.material_resources.push_back(std::move(material.snapshot));
      handle.ptr += stride;
    }
    for (const auto& binding : bindings) {
      if (std::get<5>(binding.pipeline) != 3) continue;
      for (uint32_t j = 0; j < binding.shader_texture_count; ++j) {
        const auto& material = materials[binding.shader_materials[j]];
        auto view = material.view;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        view.Texture2DArray.MostDetailedMip =
            material.view.Texture2D.MostDetailedMip;
        view.Texture2DArray.MipLevels = material.view.Texture2D.MipLevels;
        view.Texture2DArray.FirstArraySlice = 0;
        view.Texture2DArray.ArraySize = 1;
        view.Texture2DArray.PlaneSlice = material.view.Texture2D.PlaneSlice;
        view.Texture2DArray.ResourceMinLODClamp =
            material.view.Texture2D.ResourceMinLODClamp;
        auto slot = frame.materials->GetCPUDescriptorHandleForHeapStart();
        slot.ptr += SIZE_T(binding.shader_view_offset + j * 2) * stride;
        auto* snapshot = frame.material_resources[
            binding.shader_materials[j] * 2 + 1].Get();
        device->CreateShaderResourceView(snapshot, &view, slot);
        slot.ptr += stride;
        device->CreateShaderResourceView(snapshot, &view, slot);
      }
    }
    for (const auto& binding : manager_bindings) {
      if (!binding.pixel_constants) continue;
      for (uint32_t j = 0; j < 2; ++j) {
        const auto& material = materials[binding.shader_materials[j]];
        auto view = material.view;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        view.Texture2DArray.MostDetailedMip =
            material.view.Texture2D.MostDetailedMip;
        view.Texture2DArray.MipLevels = material.view.Texture2D.MipLevels;
        view.Texture2DArray.FirstArraySlice = 0;
        view.Texture2DArray.ArraySize = 1;
        view.Texture2DArray.PlaneSlice = material.view.Texture2D.PlaneSlice;
        view.Texture2DArray.ResourceMinLODClamp =
            material.view.Texture2D.ResourceMinLODClamp;
        auto slot = frame.materials->GetCPUDescriptorHandleForHeapStart();
        slot.ptr += SIZE_T(binding.shader_view_offset +
                           (j == 0 ? 5 : 2)) * stride;
        auto* snapshot = frame.material_resources[
            binding.shader_materials[j] * 2 + 1].Get();
        device->CreateShaderResourceView(snapshot, &view, slot);
        slot.ptr += stride;
        device->CreateShaderResourceView(snapshot, &view, slot);
      }
    }
    bool native_second_routed = false;
    for (const auto& binding : remainder_bindings) {
      const uint32_t kind = std::get<8>(binding.pipeline);
      if (kind != 3 && kind != 4) continue;
      const uint32_t count = kind == 3 ? 8 : 5;
      for (uint32_t j = 0; j < count; ++j) {
        const auto& material = materials[binding.shader_materials[j]];
        auto view = material.view;
        const bool cube = kind == 3 ? j >= 6 : j == 2 || j == 3;
        if (!cube) {
          view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
          view.Texture2DArray.MostDetailedMip =
              material.view.Texture2D.MostDetailedMip;
          view.Texture2DArray.MipLevels = material.view.Texture2D.MipLevels;
          view.Texture2DArray.FirstArraySlice = 0;
          view.Texture2DArray.ArraySize = 1;
          view.Texture2DArray.PlaneSlice = material.view.Texture2D.PlaneSlice;
          view.Texture2DArray.ResourceMinLODClamp =
              material.view.Texture2D.ResourceMinLODClamp;
        }
        auto slot = frame.materials->GetCPUDescriptorHandleForHeapStart();
        const uint32_t view_index = kind == 3
            ? (cube ? j - 6 : j) : (cube ? j - 2 : j == 4 ? 2 : j);
        const uint32_t offset = binding.shader_view_offset +
            (cube ? 12 : 0) + view_index * 2;
        slot.ptr += SIZE_T(offset) * stride;
        auto* snapshot = frame.material_resources[
            binding.shader_materials[j] * 2 + 1].Get();
        if (ordered_tiles && !native_second_routed &&
            binding.shader_materials[j] == guest_second_color_index) {
          snapshot = frame.initial_color_versions[1].Get();
          view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
          native_second_routed = true;
          REXGPU_WARN("FH1 RAY01 native second color consumer frame={} "
                      "sequence={}", trace_frame, binding.sequence);
        }
        device->CreateShaderResourceView(snapshot, &view, slot);
        slot.ptr += stride;
        device->CreateShaderResourceView(snapshot, &view, slot);
      }
    }
    if (ordered_tiles && !native_second_routed)
      return reject("second_color_consumer");
    for (const auto& binding : producer_bindings) {
      for (uint32_t j = 0; j < binding.material_count; ++j) {
        const auto& material = materials[binding.materials[j]];
        auto view = material.view;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        view.Texture2DArray.MostDetailedMip = 0;
        view.Texture2DArray.MipLevels = 1;
        view.Texture2DArray.FirstArraySlice = 0;
        view.Texture2DArray.ArraySize = 1;
        view.Texture2DArray.PlaneSlice = 0;
        view.Texture2DArray.ResourceMinLODClamp = 0;
        ID3D12Resource* resource = frame.material_resources[
            binding.materials[j] * 2 + 1].Get();
        if (binding.downsample) {
          resource = frame.color_tiles.Get();
          view.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
        }
        auto slot = frame.materials->GetCPUDescriptorHandleForHeapStart();
        slot.ptr += SIZE_T(binding.view_offset + j * 2) * stride;
        device->CreateShaderResourceView(resource, &view, slot);
        slot.ptr += stride;
        device->CreateShaderResourceView(resource, &view, slot);
      }
    }
  }
  {
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    // Bindless translations address samplers at 1, 4, 7, ... 22.
    heap.NumDescriptors = 2 + 3 * 32;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(
            &heap, IID_PPV_ARGS(&frame.samplers))))
      return reject("sampler_heap");
    const auto stride = device->GetDescriptorHandleIncrementSize(heap.Type);
    const auto start = frame.samplers->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < heap.NumDescriptors; ++i) {
      D3D12_SAMPLER_DESC sampler{};
      sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
      const uint32_t block = i >= 2 ? (i - 2) / 32 : 0;
      const uint32_t sampler_slot = i >= 2 ? (i - 2) % 32 : 0;
      // ponytail: shared wrap/clamp tables; use per-fetch address modes if
      // a material proves this approximation visibly wrong.
      const bool clamp = i == 1 ||
          (block == 1 && (sampler_slot == 19 || sampler_slot == 22)) ||
          (block == 2 && (sampler_slot == 7 || sampler_slot == 10));
      sampler.AddressU = clamp ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP
                               : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
      sampler.AddressV = sampler.AddressU;
      sampler.AddressW = sampler.AddressU;
      sampler.MaxAnisotropy = 1;
      sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
      sampler.MaxLOD = D3D12_FLOAT32_MAX;
      auto slot = start;
      slot.ptr += SIZE_T(i) * stride;
      device->CreateSampler(&sampler, slot);
    }
  }
  list->ReserveAdditionalBytes(
      (scene.draws.size() + item_bindings.size() +
       character_bindings.size() +
       vegetation_bindings.size() + manager_bindings.size() +
       remainder_bindings.size() + producer_bindings.size() +
       materials.size()) * 512 + 8192);
  const auto base = frame.upload->GetGPUVirtualAddress();
  const auto rtv = frame.rtv->GetCPUDescriptorHandleForHeapStart();
  auto output_rtv = rtv;
  output_rtv.ptr += device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  auto offscreen_rtv = output_rtv;
  offscreen_rtv.ptr += device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  auto downsample_rtv = offscreen_rtv;
  downsample_rtv.ptr += device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  const auto dsv = frame.dsv->GetCPUDescriptorHandleForHeapStart();
  auto offscreen_dsv = dsv;
  offscreen_dsv.ptr += device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
  auto* scene_color = frame.color.Get();
  auto* scene_depth = frame.depth.Get();
  auto* color_tiles = frame.color_tiles.Get();
  auto* downsample_color = frame.downsample.Get();
  auto* offscreen = frame.offscreen.Get();
  auto* offscreen_depth = frame.offscreen_depth.Get();
  std::array<ID3D12Resource*, 2> initial_color_versions{};
  for (size_t i = 0; i < initial_color_versions.size(); ++i)
    initial_color_versions[i] = frame.initial_color_versions[i].Get();
  std::array<ID3D12Resource*, 3> depth_versions{};
  for (size_t i = 0; i < depth_versions.size(); ++i)
    depth_versions[i] = frame.depth_versions[i].Get();
  auto* hud = frame.hud.Get();
  auto* scene_srv = frame.srv.Get();
  auto* material_heap = frame.materials.Get();
  auto* sampler_heap = frame.samplers.Get();
  const auto material_gpu_start = material_heap
      ? material_heap->GetGPUDescriptorHandleForHeapStart()
      : D3D12_GPU_DESCRIPTOR_HANDLE{};
  const auto material_stride = device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  const auto sampler_gpu_start = sampler_heap->GetGPUDescriptorHandleForHeapStart();
  const auto sampler_stride = device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  auto scene_srv_gpu = frame.srv->GetGPUDescriptorHandleForHeapStart();
  if (ordered_tiles)
    scene_srv_gpu.ptr += (downsample_probe ? 4 : 2) * material_stride;
  graphics.submitted.emplace_back(context.submission, std::move(frame));

  // Snapshot before any later cache upload can refresh the same allocation.
  // Enqueue ownership first, so even a later render failure retains both
  // resources until this submission completes.
  const auto& retained = graphics.submitted.back().second.material_resources;
  for (size_t i = 0; i < materials.size(); ++i) {
    if (materials[i].pinned) continue;
    auto* source = retained[i * 2].Get();
    auto* snapshot = retained[i * 2 + 1].Get();
    D3D12_RESOURCE_BARRIER copy_barrier{};
    copy_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    copy_barrier.Transition.pResource = source;
    copy_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    copy_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    copy_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->D3DResourceBarrier(1, &copy_barrier);
    list->D3DCopyResource(snapshot, source);
    std::swap(copy_barrier.Transition.StateBefore,
              copy_barrier.Transition.StateAfter);
    list->D3DResourceBarrier(1, &copy_barrier);
    copy_barrier.Transition.pResource = snapshot;
    copy_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    copy_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    list->D3DResourceBarrier(1, &copy_barrier);
  }

  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  if (!scene_only) {
    barrier.Transition.pResource = output;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATES(
        context.guest_output_state);
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->D3DResourceBarrier(1, &barrier);
    list->D3DCopyResource(hud, output);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->D3DResourceBarrier(1, &barrier);
  }
  const float sky[]{0.11f, 0.22f, 0.43f, 1.f};
  list->D3DClearRenderTargetView(rtv, sky, 0, nullptr);
  list->D3DClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0, 0, 0,
                                 nullptr);
  list->D3DOMSetRenderTargets(1, &rtv, FALSE, &dsv);
  list->SetDescriptorHeaps(material_heap, sampler_heap);
  list->D3DSetGraphicsRootSignature(graphics.root.Get());
  list->D3DSetGraphicsRootDescriptorTable(10, sampler_gpu_start);
  auto draw_track = [&](size_t i) {
    const auto& draw = scene.draws[i];
    const auto& binding = bindings[i];
    list->RSSetViewport(binding.viewport);
    list->RSSetScissorRect(binding.scissor);
    list->D3DIASetPrimitiveTopology(draw.primitive == 4
        ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
        : D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    D3D12_INDEX_BUFFER_VIEW index{base + binding.index,
                                  draw.index.second, DXGI_FORMAT_R16_UINT};
    list->D3DIASetIndexBuffer(&index);
    list->D3DSetPipelineState(graphics.pipelines.at(binding.pipeline).Get());
    list->D3DSetGraphicsRootConstantBufferView(0, base + binding.b0);
    list->D3DSetGraphicsRootConstantBufferView(1, base + binding.b1);
    list->D3DSetGraphicsRootConstantBufferView(2, base + binding.b3);
    list->D3DSetGraphicsRootShaderResourceView(3, base + binding.vertex);
    if (binding.material_index != UINT32_MAX) {
      auto handle = material_gpu_start;
      handle.ptr += SIZE_T(binding.material_index) * material_stride;
      list->D3DSetGraphicsRootDescriptorTable(6, handle);
    }
    if (binding.pixel_constants) {
      list->D3DSetGraphicsRootConstantBufferView(7,
                                                 base + binding.pixel_descriptors);
      list->D3DSetGraphicsRootConstantBufferView(8,
                                                 base + binding.pixel_constants);
      list->D3DSetGraphicsRootConstantBufferView(9, base + binding.pixel_bool);
      auto sampler = sampler_gpu_start;
      sampler.ptr += SIZE_T(binding.shader_sampler_offset) * sampler_stride;
      list->D3DSetGraphicsRootDescriptorTable(10, sampler);
      auto textures = material_gpu_start;
      textures.ptr += SIZE_T(binding.shader_view_offset) * material_stride;
      list->D3DSetGraphicsRootDescriptorTable(11, textures);
    }
    const uint64_t hash = draw.pixel_shader;
    const float color[]{0.19f + float(hash & 255) / 1024.f,
                        0.23f + float((hash >> 8) & 255) / 1024.f,
                        0.17f + float((hash >> 16) & 255) / 1024.f, 1.f};
    list->D3DSetGraphicsRoot32BitConstants(4, 4, color, 0);
    list->D3DDrawIndexedInstanced(draw.count, 1, 0, 0, 0);
    if (binding.pixel_constants)
      list->D3DSetGraphicsRootDescriptorTable(10, sampler_gpu_start);
  };
  D3D12_INDEX_BUFFER_VIEW item_view{base + item_index, item_index_bytes,
                                     DXGI_FORMAT_R16_UINT};
  auto draw_item = [&](size_t i) {
    const auto& binding = item_bindings[i];
    list->RSSetViewport({0, 0, 1280, 720, 0, 0.5f});
    list->RSSetScissorRect({0, 0, 1280, 720});
    list->D3DIASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->D3DIASetIndexBuffer(&item_view);
    list->D3DSetPipelineState(graphics.item_pipelines.at(
        {binding.shader, binding.specialization, false}).Get());
    list->D3DSetGraphicsRootConstantBufferView(0, base + binding.b0);
    list->D3DSetGraphicsRootConstantBufferView(1, base + binding.b1);
    list->D3DSetGraphicsRootConstantBufferView(2, base + binding.b3);
    list->D3DSetGraphicsRootShaderResourceView(3, base + binding.vertex);
    const float color[]{0.47f, 0.40f, 0.31f, 1.f};
    list->D3DSetGraphicsRoot32BitConstants(4, 4, color, 0);
    list->D3DDrawIndexedInstanced(binding.count, 1, 0, 0, 0);
  };
  D3D12_INDEX_BUFFER_VIEW vegetation_view{
      base + vegetation_index, vegetation_index_bytes, DXGI_FORMAT_R16_UINT};
  auto draw_vegetation = [&](size_t i) {
    const auto& binding = vegetation_bindings[i];
    list->RSSetViewport({0, 0, 1280, 720, 0, 0.5f});
    list->RSSetScissorRect({0, 0, 1280, 720});
    list->D3DIASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->D3DIASetIndexBuffer(&vegetation_view);
    list->D3DSetPipelineState(graphics.item_pipelines.at(
        {binding.shader, binding.specialization, binding.alpha}).Get());
    list->D3DSetGraphicsRootConstantBufferView(0, base + binding.b0);
    list->D3DSetGraphicsRootConstantBufferView(1, base + binding.b1);
    list->D3DSetGraphicsRootConstantBufferView(2, base + binding.b3);
    list->D3DSetGraphicsRootShaderResourceView(3, base + binding.vertex);
    if (binding.material_index != UINT32_MAX) {
      auto handle = material_gpu_start;
      handle.ptr += SIZE_T(binding.material_index) * material_stride;
      list->D3DSetGraphicsRootDescriptorTable(6, handle);
    }
    const float color[]{0.18f, 0.36f, 0.19f, 1.f};
    list->D3DSetGraphicsRoot32BitConstants(4, 4, color, 0);
    list->D3DDrawIndexedInstanced(binding.count, 1, 0, 0, 0);
  };
  D3D12_INDEX_BUFFER_VIEW character_view{
      base + character_index, character_index_bytes, DXGI_FORMAT_R16_UINT};
  auto draw_character = [&](size_t i) {
    const auto& binding = character_bindings[i];
    list->D3DIASetIndexBuffer(&character_view);
    list->D3DIASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->RSSetViewport(binding.viewport);
    list->RSSetScissorRect(binding.scissor);
    list->D3DSetPipelineState(graphics.item_pipelines.at(
        {binding.shader, binding.specialization, false}).Get());
    list->D3DSetGraphicsRootConstantBufferView(0, base + binding.b0);
    list->D3DSetGraphicsRootConstantBufferView(1, base + binding.b1);
    list->D3DSetGraphicsRootConstantBufferView(2, base + binding.b3);
    list->D3DSetGraphicsRootShaderResourceView(3, base + binding.vertex);
    const float color[]{0.36f, 0.32f, 0.28f, 1.f};
    list->D3DSetGraphicsRoot32BitConstants(4, 4, color, 0);
    list->D3DDrawIndexedInstanced(binding.count, 1, 0, 0, 0);
  };
  auto draw_manager = [&](size_t i) {
    const auto& binding = manager_bindings[i];
    list->D3DSetGraphicsRootShaderResourceView(3, base + manager_vertex);
    list->D3DIASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->D3DSetPipelineState(binding.pixel_constants
        ? graphics.manager_original_pipeline.Get()
        : graphics.manager_pipeline.Get());
    list->RSSetViewport(binding.viewport);
    list->RSSetScissorRect(binding.scissor);
    D3D12_INDEX_BUFFER_VIEW index{base + binding.index,
                                  binding.index_bytes,
                                  DXGI_FORMAT_R16_UINT};
    list->D3DIASetIndexBuffer(&index);
    list->D3DSetGraphicsRootConstantBufferView(0, base + binding.b0);
    list->D3DSetGraphicsRootConstantBufferView(1, base + binding.b1);
    list->D3DSetGraphicsRootConstantBufferView(2, base + binding.b3);
    if (binding.pixel_constants) {
      list->D3DSetGraphicsRootConstantBufferView(7, base + binding.b4);
      list->D3DSetGraphicsRootConstantBufferView(8,
                                                 base + binding.pixel_constants);
      list->D3DSetGraphicsRootConstantBufferView(9, base + binding.pixel_bool);
      auto sampler = sampler_gpu_start;
      sampler.ptr += 2 * sampler_stride;
      list->D3DSetGraphicsRootDescriptorTable(10, sampler);
      auto textures = material_gpu_start;
      textures.ptr += SIZE_T(binding.shader_view_offset) * material_stride;
      list->D3DSetGraphicsRootDescriptorTable(11, textures);
    }
    const float color[]{0.32f, 0.29f, 0.27f, 1.f};
    list->D3DSetGraphicsRoot32BitConstants(4, 4, color, 0);
    list->D3DDrawIndexedInstanced(binding.count, 1, 0, 0, 0);
    if (binding.pixel_constants)
      list->D3DSetGraphicsRootDescriptorTable(10, sampler_gpu_start);
  };
  auto draw_remainder = [&](size_t i) {
    const auto& binding = remainder_bindings[i];
    list->D3DSetGraphicsRootShaderResourceView(3, base + remainder_vertex);
    list->RSSetViewport(binding.viewport);
    list->RSSetScissorRect(binding.scissor);
    list->D3DIASetPrimitiveTopology(binding.primitive == 4
        ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
        : D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    D3D12_INDEX_BUFFER_VIEW index{base + binding.index,
                                  binding.index_bytes,
                                  binding.format ? DXGI_FORMAT_R32_UINT
                                                 : DXGI_FORMAT_R16_UINT};
    list->D3DIASetIndexBuffer(&index);
    list->D3DSetPipelineState(graphics.remainder_pipelines.at(
        binding.pipeline).Get());
    list->D3DSetGraphicsRootConstantBufferView(0, base + binding.b0);
    list->D3DSetGraphicsRootConstantBufferView(1, base + binding.b1);
    list->D3DSetGraphicsRootConstantBufferView(2, base + binding.b3);
    if (binding.b4)
      list->D3DSetGraphicsRootConstantBufferView(7, base + binding.b4);
    if (binding.pixel_constants)
      list->D3DSetGraphicsRootConstantBufferView(8,
                                                 base + binding.pixel_constants);
    if (binding.shader_bool) {
      list->D3DSetGraphicsRootConstantBufferView(9, base + binding.shader_bool);
      auto sampler = sampler_gpu_start;
      sampler.ptr += SIZE_T(binding.shader_sampler_offset) * sampler_stride;
      list->D3DSetGraphicsRootDescriptorTable(10, sampler);
      auto textures = material_gpu_start;
      textures.ptr += SIZE_T(binding.shader_view_offset) * material_stride;
      list->D3DSetGraphicsRootDescriptorTable(11, textures);
      textures.ptr += 12 * material_stride;
      list->D3DSetGraphicsRootDescriptorTable(12, textures);
    }
    if (binding.material_index != UINT32_MAX) {
      auto handle = material_gpu_start;
      handle.ptr += SIZE_T(binding.material_index) * material_stride;
      list->D3DSetGraphicsRootDescriptorTable(6, handle);
    }
    list->D3DSetGraphicsRoot32BitConstants(4, 4, binding.color.data(), 0);
    list->D3DDrawIndexedInstanced(binding.count, 1, 0, 0, 0);
    if (binding.shader_bool)
      list->D3DSetGraphicsRootDescriptorTable(10, sampler_gpu_start);
  };
  auto draw_producer = [&](const ProducerDrawBinding& binding) {
    if (binding.downsample)
      list->D3DOMSetRenderTargets(1, &downsample_rtv, FALSE, nullptr);
    list->RSSetViewport(binding.viewport);
    list->RSSetScissorRect(binding.scissor);
    list->D3DIASetPrimitiveTopology(binding.downsample
        ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
        : D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    if (!binding.indexed) {
      list->D3DIASetIndexBuffer(nullptr);
    } else {
      D3D12_INDEX_BUFFER_VIEW index{base + binding.index,
                                    binding.count * 2,
                                    DXGI_FORMAT_R16_UINT};
      list->D3DIASetIndexBuffer(&index);
    }
    list->D3DSetPipelineState(graphics.producer_pipelines.at(
        binding.pipeline).Get());
    list->D3DSetGraphicsRootConstantBufferView(0, base + binding.b0);
    list->D3DSetGraphicsRootConstantBufferView(1, base + binding.b1);
    list->D3DSetGraphicsRootConstantBufferView(2, base + binding.b3);
    list->D3DSetGraphicsRootShaderResourceView(3, base + binding.vertex);
    list->D3DSetGraphicsRootConstantBufferView(7,
                                               base + binding.descriptors);
    list->D3DSetGraphicsRootConstantBufferView(8,
                                               base + binding.pixel_constants);
    list->D3DSetGraphicsRootConstantBufferView(9, base + binding.bool_loop);
    auto sampler = sampler_gpu_start;
    sampler.ptr += 2 * sampler_stride;
    list->D3DSetGraphicsRootDescriptorTable(10, sampler);
    auto textures = material_gpu_start;
    textures.ptr += SIZE_T(binding.view_offset) * material_stride;
    list->D3DSetGraphicsRootDescriptorTable(11, textures);
    list->D3DSetGraphicsRootDescriptorTable(12, textures);
    const float fallback[]{1.f, 1.f, 1.f, 1.f};
    list->D3DSetGraphicsRoot32BitConstants(4, 4, fallback, 0);
    if (binding.indexed)
      list->D3DDrawIndexedInstanced(binding.count, 1, 0, 0, 0);
    else
      list->D3DDrawInstanced(binding.count, 1, 0, 0);
    list->D3DSetGraphicsRootDescriptorTable(10, sampler_gpu_start);
    if (binding.downsample)
      list->D3DOMSetRenderTargets(1, &rtv, FALSE, &dsv);
  };
  using OrderedDraw = std::tuple<uint64_t, uint8_t, size_t>;
  std::vector<OrderedDraw> order;
  order.reserve(scene.draws.size() + item_bindings.size() +
                vegetation_bindings.size() + character_bindings.size() +
                manager_bindings.size() + remainder_bindings.size());
  for (size_t i = 0; i < scene.draws.size(); ++i)
    order.emplace_back(scene.draws[i].sequence, 0, i);
  for (size_t i = 0; i < item_bindings.size(); ++i)
    order.emplace_back(item_bindings[i].sequence, 1, i);
  for (size_t i = 0; i < vegetation_bindings.size(); ++i)
    order.emplace_back(vegetation_bindings[i].sequence, 2, i);
  for (size_t i = 0; i < character_bindings.size(); ++i)
    order.emplace_back(character_bindings[i].sequence, 3, i);
  for (size_t i = 0; i < manager_bindings.size(); ++i)
    order.emplace_back(manager_bindings[i].sequence, 4, i);
  for (size_t i = 0; i < remainder_bindings.size(); ++i)
    order.emplace_back(remainder_bindings[i].sequence, 5, i);
  if (order.size() > 8192) return reject("draw_order_size");
  std::sort(order.begin(), order.end());
  for (size_t i = 0; i < order.size(); ++i) {
    if (!std::get<0>(order[i]) ||
        (i && std::get<0>(order[i - 1]) == std::get<0>(order[i])))
      return reject("draw_order");
  }
  const auto issue_draw = [&](uint8_t family, size_t index) {
    switch (family) {
      case 0: draw_track(index); break;
      case 1: draw_item(index); break;
      case 2: draw_vegetation(index); break;
      case 3: draw_character(index); break;
      case 4: draw_manager(index); break;
      case 5: draw_remainder(index); break;
    }
  };
  // Scene ownership uses output - 1; prepared GPU events keep the output frame.
  if (ordered_tiles) {
    if (order.empty()) return reject("ordered_source_empty");
    const auto& operations = ordered_operations;
    if (!operations) return reject("ordered_stream");
    std::map<uint64_t, std::pair<uint8_t, size_t>> supported;
    for (const auto& [sequence, family, index] : order)
      supported.emplace(sequence, std::pair{family, index});
    size_t matched = 0, unsupported = 0, copies = 0, clears = 0;
    for (const auto& event : *operations) {
      if (event.kind == 'D') {
        matched += supported.contains(event.sequence);
        unsupported += !supported.contains(event.sequence);
      } else if (event.kind == 'C') {
        ++copies;
      } else if (event.kind == 'K') {
        ++clears;
      } else {
        return reject("ordered_event_kind");
      }
    }
    REXGPU_WARN("FH1 RAY01 ordered dispatch frame={} scene_frame={} supported={} "
                "unsupported={} copies={} clears={} source_draws={} "
                "source_range={}:{} event_range={}:{}",
                trace_frame, scene.source_frame, matched, unsupported, copies,
                clears,
                order.size(), std::get<0>(order.front()),
                std::get<0>(order.back()), operations->front().sequence,
                operations->back().sequence);
    if (const char* root = std::getenv("PINYON_SHIFT_FH1_RENDER_TEST_OUTPUT");
        root && *root) {
      std::ofstream manifest(std::filesystem::path(root) /
          ("ordered-native-support-" + std::to_string(trace_frame) + ".csv"));
      if (manifest) {
        manifest << "sequence,family\n";
        for (const auto& event : *operations)
          if (event.kind == 'D') {
            const auto found = supported.find(event.sequence);
            manifest << event.sequence << ',' << (found == supported.end()
                ? -1 : int(found->second.first)) << '\n';
          }
      }
    }
    if (matched != order.size()) return reject("ordered_draw_mismatch");
    for (const auto& binding : producer_bindings)
      if (supported.contains(binding.sequence))
        return reject("ordered_producer_overlap");
    const auto restore_offscreen_depth = [&] {
      barrier.Transition.pResource = offscreen_depth;
      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_READ;
      barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
      list->D3DResourceBarrier(1, &barrier);
    };
    const auto reject_ordered = [&](const char* stage) {
      restore_offscreen_depth();
      return reject(stage);
    };
    barrier.Transition.pResource = offscreen_depth;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_READ;
    list->D3DResourceBarrier(1, &barrier);
    const auto first_draw = std::lower_bound(
        operations->begin(), operations->end(), std::get<0>(order.front()),
        [](const OrderedFrameOperation& event, uint64_t sequence) {
          return event.sequence < sequence;
        });
    const OrderedFrameOperation* main_clear = nullptr;
    if (first_draw != operations->begin() && first_draw != operations->end() &&
        first_draw->kind == 'D') {
      const auto& clear = *std::prev(first_draw);
      const auto& bounds = clear.bounds[0];
      if (clear.kind == 'K' && clear.sequence + 1 == first_draw->sequence &&
          clear.clear_mode == 0 && (clear.clear_flags & 3) == 3 &&
          clear.rectangle_count == 1 &&
          clear.surface == first_draw->surface &&
          clear.depth == first_draw->depth &&
          (clear.color & 0xFFFFu) == (first_draw->color & 0xFFFFu) &&
          clear.target_bits == first_draw->target_bits &&
          bounds[0] == 0 && bounds[1] == 0 && bounds[2] == 1280 &&
          bounds[3] > 0 && bounds[3] <= 720 &&
          std::isfinite(clear.clear_depth[0]) &&
          std::all_of(clear.clear_color[0].begin(),
                      clear.clear_color[0].end(),
                      [](float value) { return std::isfinite(value); }))
        main_clear = &clear;
    }
    uint32_t copied_height = 0, tile_copies = 0, tile_base = 0;
    uint32_t depth_height = 0, depth_copies = 0, depth_base = 0;
    uint32_t initial_color_copies = 0;
    bool downsample_copied = false;
    size_t producer_index = 0;
    for (const auto& event : *operations) {
      if (event.sequence == producer_clear_sequence) {
        if (event.kind != 'K' || !(event.clear_flags & 1) ||
            event.rectangle_count != 1 ||
            event.bounds[0] != std::array<int32_t, 4>{0, 0, 640, 360})
          return reject_ordered("ordered_producer_clear");
        list->D3DClearRenderTargetView(offscreen_rtv,
                                      event.clear_color[0].data(), 0, nullptr);
        list->D3DOMSetRenderTargets(1, &offscreen_rtv, FALSE,
                                   &offscreen_dsv);
      }
      if (main_clear && event.sequence == main_clear->sequence) {
        const auto& bounds = event.bounds[0];
        const D3D12_RECT rect{bounds[0], bounds[1], bounds[2], bounds[3]};
        list->D3DClearRenderTargetView(rtv, event.clear_color[0].data(), 1,
                                      &rect);
        list->D3DClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH,
                                      event.clear_depth[0], 0, 1, &rect);
        REXGPU_WARN("FH1 RAY01 ordered main clear frame={} sequence={} "
                    "height={}", trace_frame, event.sequence, bounds[3]);
      }
      if (event.kind == 'C' && event.copy.source_format == 1 &&
          (event.copy.control & 7) == 4 &&
          event.copy.source_base_tiles == 1024 &&
          event.resolve_width == 1280 && event.resolve_height) {
        const auto height = event.resolve_height;
        if (!event.succeeded || !event.copy.info_valid ||
            !event.copy.source_available || depth_copies >= 3 ||
            height != (depth_copies == 2 ? 208u : 256u) ||
            event.copy.physical_x || event.copy.physical_y ||
            event.copy.physical_width != 1280 ||
            event.copy.physical_height != height ||
            event.copy.dest_x || event.copy.dest_y ||
            event.copy.dest_pitch != 1280)
          return reject_ordered("ordered_depth_tile_shape");
        if (!depth_copies) depth_base = event.dest_base;
        if (uint64_t(event.dest_base) !=
            uint64_t(depth_base) + uint64_t(depth_height) * 1280 * 4)
          return reject_ordered("ordered_depth_tile_address");
        barrier.Transition.pResource = scene_depth;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->D3DResourceBarrier(1, &barrier);
        list->D3DCopyResource(depth_versions[depth_copies], scene_depth);
        std::swap(barrier.Transition.StateBefore,
                  barrier.Transition.StateAfter);
        list->D3DResourceBarrier(1, &barrier);
        depth_height += height;
        ++depth_copies;
        REXGPU_WARN("FH1 RAY01 ordered depth tile frame={} sequence={} "
                    "height={} total={}", trace_frame, event.sequence,
                    height, depth_height);
      }
      if (event.kind == 'C' && event.dest_base == 484626432 &&
          event.copy.source_base_tiles == 720) {
        if (!event.succeeded || !event.copy.info_valid ||
            !event.copy.source_available || initial_color_copies >= 2 ||
            event.copy.source_format != 0 ||
            (event.copy.control & 7) != 0 ||
            event.resolve_width != 1280 || event.resolve_height != 720 ||
            event.copy.physical_x || event.copy.physical_y ||
            event.copy.physical_width != 1280 ||
            event.copy.physical_height != 720 ||
            event.copy.dest_x || event.copy.dest_y ||
            event.copy.dest_pitch != 1280 ||
            producer_index != (initial_color_copies ? 12u : 11u))
          return reject_ordered("ordered_initial_color_shape");
        barrier.Transition.pResource = offscreen;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->D3DResourceBarrier(1, &barrier);
        list->D3DCopyResource(initial_color_versions[initial_color_copies],
                              offscreen);
        std::swap(barrier.Transition.StateBefore,
                  barrier.Transition.StateAfter);
        list->D3DResourceBarrier(1, &barrier);
        if (!initial_color_copies) {
          barrier.Transition.pResource = initial_color_versions[0];
          barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
          barrier.Transition.StateAfter =
              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          list->D3DResourceBarrier(1, &barrier);
          // The guest resolve owns tiles not materialized by the first 1x
          // draw. Seed untouched feedback pixels from its pinned version.
          list->D3DOMSetRenderTargets(1, &offscreen_rtv, FALSE, nullptr);
          list->RSSetViewport({0, 0, 1280, 720, 0, 1});
          list->RSSetScissorRect({0, 0, 1280, 720});
          list->SetDescriptorHeaps(material_heap, nullptr);
          list->D3DSetGraphicsRootSignature(graphics.blit_root.Get());
          list->D3DSetPipelineState(graphics.seed_pipeline.Get());
          auto seed_view = material_gpu_start;
          seed_view.ptr += SIZE_T(producer_bindings[11].view_offset) *
                           material_stride;
          list->D3DSetGraphicsRootDescriptorTable(0, seed_view);
          list->D3DIASetPrimitiveTopology(
              D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
          list->D3DDrawInstanced(3, 1, 0, 0);
          list->D3DOMSetRenderTargets(1, &offscreen_rtv, FALSE,
                                     &offscreen_dsv);
          list->SetDescriptorHeaps(material_heap, sampler_heap);
          list->D3DSetGraphicsRootSignature(graphics.root.Get());
        } else {
          barrier.Transition.pResource = initial_color_versions[1];
          barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
          barrier.Transition.StateAfter =
              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          list->D3DResourceBarrier(1, &barrier);
          list->D3DOMSetRenderTargets(1, &rtv, FALSE, &dsv);
        }
        REXGPU_WARN("FH1 RAY01 ordered initial color frame={} sequence={} "
                    "version={}", trace_frame, event.sequence,
                    initial_color_copies);
        ++initial_color_copies;
      }
      if (event.kind == 'C' && event.copy.source_format == 3 &&
          (event.copy.control & 7) == 0 &&
          event.copy.source_base_tiles == 0 &&
          event.resolve_width == 1280 && event.resolve_height) {
        const auto height = event.resolve_height;
        if (!event.succeeded || !event.copy.info_valid ||
            !event.copy.source_available || tile_copies >= 3 ||
            depth_copies != tile_copies + 1 ||
            height != (tile_copies == 2 ? 208u : 256u) ||
            event.copy.physical_x || event.copy.physical_y ||
            event.copy.physical_width != 1280 ||
            event.copy.physical_height != height ||
            event.copy.dest_x || event.copy.dest_y ||
            event.copy.dest_pitch != 1280)
          return reject_ordered("ordered_color_tile_shape");
        if (!tile_copies) tile_base = event.dest_base;
        if (uint64_t(event.dest_base) !=
            uint64_t(tile_base) + uint64_t(copied_height) * 1280 * 4)
          return reject_ordered("ordered_color_tile_address");
        barrier.Transition.pResource = scene_color;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->D3DResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
        source.pResource = scene_color;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.pResource = color_tiles;
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        const D3D12_BOX source_box{0, copied_height, 0, 1280,
                                   copied_height + height, 1};
        list->D3DCopyTextureRegion(&destination, 0, copied_height, 0,
                                   &source, &source_box);
        std::swap(barrier.Transition.StateBefore,
                  barrier.Transition.StateAfter);
        list->D3DResourceBarrier(1, &barrier);
        copied_height += height;
        ++tile_copies;
        if (tile_copies == 3) {
          barrier.Transition.pResource = color_tiles;
          barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
          barrier.Transition.StateAfter =
              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          list->D3DResourceBarrier(1, &barrier);
        }
        REXGPU_WARN("FH1 RAY01 ordered color tile frame={} sequence={} "
                    "height={} total={}", trace_frame, event.sequence,
                    height, copied_height);
      }
      if (event.kind == 'C' && event.dest_base == 480858112 &&
          event.resolve_width == 320 && event.resolve_height == 192) {
        if (downsample_copied || producer_index != 16 ||
            !event.succeeded || !event.copy.info_valid ||
            !event.copy.source_available ||
            event.copy.source_base_tiles != 0 ||
            event.copy.source_format != 3 ||
            (event.copy.control & 7) != 0 ||
            event.copy.physical_x || event.copy.physical_y ||
            event.copy.physical_width != 320 ||
            event.copy.physical_height != 192 ||
            event.copy.dest_x || event.copy.dest_y ||
            event.copy.dest_pitch != 320 ||
            event.copy.dest_height != 192)
          return reject_ordered("ordered_downsample_copy");
        barrier.Transition.pResource = downsample_color;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        list->D3DResourceBarrier(1, &barrier);
        downsample_copied = true;
      }
      if (event.kind != 'D') continue;
      if (producer_index < producer_bindings.size() &&
          event.sequence == producer_bindings[producer_index].sequence) {
        draw_producer(producer_bindings[producer_index]);
        ++producer_index;
        continue;
      }
      if (const auto found = supported.find(event.sequence);
          found != supported.end())
        issue_draw(found->second.first, found->second.second);
    }
    if (producer_index != producer_bindings.size() ||
        initial_color_copies != 2 ||
        tile_copies != 3 || copied_height != 720 ||
        depth_copies != 3 || depth_height != 720 || !downsample_copied)
      return reject_ordered("ordered_tiles_incomplete");
    REXGPU_WARN("FH1 RAY01 original draw replay frame={} "
                "offscreen_draws=12 scene_draws=3 downsample_draws=1 "
                "copies={}", trace_frame,
                initial_color_copies);
    restore_offscreen_depth();
  } else {
    for (const auto& entry : order)
      issue_draw(std::get<1>(entry), std::get<2>(entry));
  }
  barrier.Transition.pResource = scene_color;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  list->D3DResourceBarrier(1, &barrier);
  if (!pre_ui) {
    barrier.Transition.pResource = output;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATES(
        context.guest_output_state);
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    list->D3DResourceBarrier(1, &barrier);
    if (!scene_only) {
      barrier.Transition.pResource = hud;
      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
      barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
      list->D3DResourceBarrier(1, &barrier);
    }
  }
  list->D3DOMSetRenderTargets(1, &output_rtv, FALSE, nullptr);
  list->RSSetViewport({0, 0, 1280, 720, 0, 1});
  list->RSSetScissorRect({0, 0, 1280, 720});
  list->SetDescriptorHeaps(scene_srv, nullptr);
  list->D3DSetGraphicsRootSignature(graphics.blit_root.Get());
  list->D3DSetPipelineState((scene_only || downsample_probe
      ? graphics.scene_blit_pipeline : graphics.blit_pipeline).Get());
  list->D3DSetGraphicsRootDescriptorTable(0, scene_srv_gpu);
  list->D3DIASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  list->D3DDrawInstanced(3, 1, 0, 0);
  if (!pre_ui) {
    barrier.Transition.pResource = output;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATES(
        context.guest_output_state);
    list->D3DResourceBarrier(1, &barrier);
  }
  return true;
}
}  // namespace

bool DrawNativeOutputTrack(
    const rex::system::NativeGuestOutputRenderContext& context,
    const Snr04LiveScene& scene, bool scene_only) {
  static thread_local TrackGraphics graphics;
  try {
    return DrawTrack(context, scene, graphics, scene_only);
  } catch (const std::exception& error) {
    if (scene_only)
      REXGPU_WARN("FH1 native output rejected source_frame={} reason={}",
                  scene.source_frame, error.what());
    else
      REXGPU_INFO("FH1 native output rejected source_frame={} reason={}",
                  scene.source_frame, error.what());
    return false;
  }
}
}  // namespace pinyon_shift::native_renderer
