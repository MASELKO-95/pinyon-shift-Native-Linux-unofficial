#include "native_renderer/native_output_ui.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <tuple>
#include <vector>

#include <rex/graphics/d3d12/deferred_command_list.h>
#include <rex/logging.h>
#include <rex/system/interfaces/graphics.h>

#include "native_renderer/ordered_ui_capture.h"

namespace pinyon_shift::native_renderer {
namespace {
using Microsoft::WRL::ComPtr;

struct ShaderPair { uint64_t vertex, pixel, specialization; };
constexpr std::array<ShaderPair, 3> kPairs{{
    {0xED90DA6EFF5C6BCAull, 0x57B9400F6B398736ull, 3},
    {0x79034645B1CB882Bull, 0xCAE1DB68AFFA9D3Cull, 3},
    {0x984DBF6AF14DBEBDull, 0x6FDA0F1CDE67D12Full, 7},
}};
constexpr size_t kMaxUpload = 128 * 1024 * 1024;

struct Binding {
  uint64_t index, vertex, system, vertex_constants, pixel_constants, fetch;
  uint64_t descriptor_indices = 0;
  uint32_t count, kind = 0, view_offset = 0;
  std::array<rex::system::GraphicsFinalDrawTextureIdentity, 2> textures{};
  D3D12_VIEWPORT viewport;
  D3D12_RECT scissor;
};

struct Upload {
  std::vector<uint8_t> bytes;

  bool Add(const void* source, size_t size, uint64_t& offset) {
    const size_t aligned = (bytes.size() + 255) & ~size_t(255);
    if (aligned > kMaxUpload || size > kMaxUpload - aligned) return false;
    bytes.resize(aligned + size);
    if (size) std::memcpy(bytes.data() + aligned, source, size);
    offset = aligned;
    return true;
  }
};

struct Frame {
  ComPtr<ID3D12Resource> upload;
  ComPtr<ID3D12DescriptorHeap> rtv, views, samplers;
  std::vector<ComPtr<ID3D12Resource>> textures;
};

struct Graphics {
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12RootSignature> root;
  std::array<ComPtr<ID3D12PipelineState>, 3> pipelines;
  ComPtr<ID3D12Resource> dummy_uav;
  std::deque<std::pair<uint64_t, Frame>> submitted;

  bool Ready(const rex::system::NativeGuestOutputRenderContext& context) {
    auto* current = static_cast<ID3D12Device*>(context.device);
    if (device.Get() != current) {
      submitted.clear();
      root.Reset();
      for (auto& pipeline : pipelines) pipeline.Reset();
      dummy_uav.Reset();
      device = current;
    }
    if (pipelines[0] && pipelines[1] && pipelines[2] && dummy_uav)
      return true;
    for (auto& pipeline : pipelines) pipeline.Reset();
    root.Reset();
    if (!device || !context.shader) return false;
    D3D12_ROOT_PARAMETER parameters[10]{};
    for (uint32_t i = 0; i < 6; ++i) {
      parameters[i].ParameterType = i == 4 ? D3D12_ROOT_PARAMETER_TYPE_SRV
                                  : i == 5 ? D3D12_ROOT_PARAMETER_TYPE_UAV
                                           : D3D12_ROOT_PARAMETER_TYPE_CBV;
      parameters[i].Descriptor.ShaderRegister =
          i == 3 ? 3 : i >= 4 ? 0 : i == 2 ? 1 : i == 1 ? 1 : 0;
      parameters[i].ShaderVisibility = i == 2
          ? D3D12_SHADER_VISIBILITY_PIXEL
          : i == 0 ? D3D12_SHADER_VISIBILITY_ALL
                   : D3D12_SHADER_VISIBILITY_VERTEX;
    }
    parameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[6].Descriptor.ShaderRegister = 3;
    parameters[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[7].Descriptor.ShaderRegister = 4;
    parameters[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_DESCRIPTOR_RANGE sampler_range{};
    sampler_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    sampler_range.NumDescriptors = UINT_MAX;
    parameters[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[8].DescriptorTable = {1, &sampler_range};
    parameters[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_DESCRIPTOR_RANGE texture_range{};
    texture_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    texture_range.NumDescriptors = UINT_MAX;
    texture_range.RegisterSpace = 1;
    parameters[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[9].DescriptorTable = {1, &texture_range};
    parameters[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC root_desc{
        10, parameters, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    ComPtr<ID3DBlob> serialized, errors;
    if (FAILED(D3D12SerializeRootSignature(
            &root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
            &serialized, &errors)) ||
        FAILED(device->CreateRootSignature(
            0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
            IID_PPV_ARGS(&root))))
      return false;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = root.Get();
    auto& target = desc.BlendState.RenderTarget[0];
    target.BlendEnable = TRUE;
    target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    target.BlendOp = D3D12_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D12_BLEND_ONE;
    target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = FALSE;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
    desc.SampleDesc.Count = 1;
    for (size_t i = 0; i < kPairs.size(); ++i) {
      const auto& pair = kPairs[i];
      const uint8_t *vertex = nullptr, *pixel = nullptr;
      size_t vertex_size = 0, pixel_size = 0;
      if (!context.shader(context, 0, pair.vertex, pair.specialization,
                          &vertex, &vertex_size) ||
          !context.shader(context, 1, pair.pixel, pair.specialization,
                          &pixel, &pixel_size) ||
          !vertex || !pixel || vertex_size < 4 || pixel_size < 4 ||
          std::memcmp(vertex, "DXBC", 4) || std::memcmp(pixel, "DXBC", 4))
        return false;
      desc.VS = {vertex, vertex_size};
      desc.PS = {pixel, pixel_size};
      const HRESULT result = device->CreateGraphicsPipelineState(
          &desc, IID_PPV_ARGS(&pipelines[i]));
      if (FAILED(result)) {
        REXGPU_WARN("FH1 UI pipeline rejected pair={} hresult={:08X}",
                    i, uint32_t(result));
        return false;
      }
    }

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = 4096;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    buffer.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&dummy_uav)))) {
      for (auto& pipeline : pipelines) pipeline.Reset();
      root.Reset();
      return false;
    }
    return true;
  }
};

bool Prepare(const std::map<uint64_t, OrderedUiDraw>& draws,
             Upload& upload, std::vector<Binding>& bindings,
             size_t& skipped) {
  skipped = 0;
  for (const auto& [sequence, draw] : draws) {
    size_t kind = 0;
    while (kind < kPairs.size() &&
           (draw.vertex_shader != kPairs[kind].vertex ||
            draw.pixel_shader != kPairs[kind].pixel ||
            draw.vertex_specialization != kPairs[kind].specialization ||
            draw.pixel_specialization != kPairs[kind].specialization)) ++kind;
    if (kind == kPairs.size()) {
      ++skipped;
      continue;
    }
    if (draw.vertices.size() != 1 ||
        draw.texture_fetches.size() != kind || draw.textures.size() != kind ||
        draw.primitive != 4 || draw.index_type != 1 ||
        draw.index_endianness != 1 || !draw.index_count ||
        uint64_t(draw.index_count) * 2 > draw.indices.size() ||
        draw.vertices[0].constant >= 96 ||
        draw.viewport[0] != 0 || draw.viewport[1] != 0 ||
        draw.viewport[2] != 1280 || !std::isfinite(draw.viewport[3]) ||
        draw.viewport[3] <= 0 || draw.viewport[3] > 720 ||
        !std::isfinite(draw.viewport[4]) ||
        !std::isfinite(draw.viewport[5]) ||
        draw.scissor[0] < 0 || draw.scissor[1] < 0 ||
        draw.scissor[0] > draw.scissor[2] ||
        draw.scissor[1] > draw.scissor[3] ||
        draw.scissor[2] > 1280 || draw.scissor[3] > 720)
      return false;
    Binding binding{};
    binding.count = draw.index_count;
    binding.kind = uint32_t(kind);
    for (size_t fetch = 0; fetch < kind; ++fetch) {
      const auto found = std::find_if(draw.textures.begin(), draw.textures.end(),
                                      [fetch](const auto& texture) {
                                        return texture.fetch_constant == fetch;
                                      });
      if (found == draw.textures.end() || found->outdated_mask ||
          !found->allocation_id || !found->payload_generation) return false;
      binding.textures[fetch] = *found;
    }
    // The original VS applies the guest index endianness from b0 itself.
    const auto& index = draw.indices;
    const uint32_t vertex_bytes = draw.vertices[0].stride * 4;
    if (!vertex_bytes || draw.vertices[0].bytes.size() < vertex_bytes)
      return false;
    for (size_t i = 0; i < size_t(draw.index_count) * 2; i += 2) {
      const uint32_t vertex = (uint32_t(index[i]) << 8) | index[i + 1];
      if (uint64_t(vertex + 1) * vertex_bytes >
          draw.vertices[0].bytes.size()) return false;
    }
    if (!upload.Add(index.data(), size_t(draw.index_count) * 2,
                    binding.index) ||
        !upload.Add(draw.vertices[0].bytes.data(),
                    draw.vertices[0].bytes.size(), binding.vertex))
      return false;
    std::array<uint32_t, 120> system{};
    std::copy(draw.system.begin(), draw.system.end(), system.begin());
    std::array<uint32_t, 1024> vertex_constants{}, pixel_constants{};
    uint32_t vertex_words = 0, pixel_words = 0;
    for (uint32_t reg = 0; reg < 256; ++reg) {
      if (draw.vertex_bitmap[reg / 64] & (uint64_t(1) << (reg % 64))) {
        std::copy_n(draw.constants.data() + reg * 4, 4,
                    vertex_constants.data() + vertex_words);
        vertex_words += 4;
      }
      if (draw.pixel_bitmap[reg / 64] & (uint64_t(1) << (reg % 64))) {
        std::copy_n(draw.constants.data() + (256 + reg) * 4, 4,
                    pixel_constants.data() + pixel_words);
        pixel_words += 4;
      }
    }
    auto fetch = draw.fetch_constants;
    const size_t slot = draw.vertices[0].constant * 2;
    if ((fetch[slot] & 0x1FFFFFFC) !=
            (draw.vertices[0].base & 0x1FFFFFFC) ||
        (fetch[slot + 1] & 0x03FFFFFC) != draw.vertices[0].length)
      return false;
    fetch[slot] &= 3;  // Root SRV already points at this draw's vertex bytes.
    if (!upload.Add(system.data(), sizeof(system), binding.system) ||
        !upload.Add(vertex_constants.data(), sizeof(vertex_constants),
                    binding.vertex_constants) ||
        !upload.Add(pixel_constants.data(), sizeof(pixel_constants),
                    binding.pixel_constants) ||
        !upload.Add(fetch.data(), sizeof(fetch), binding.fetch))
      return false;
    if (kind) {
      std::array<uint32_t, 16> indices{};
      indices[1] = 0; indices[2] = 0; indices[3] = 1;
      indices[4] = 1; indices[5] = 2; indices[6] = 3;
      if (!upload.Add(indices.data(), sizeof(indices),
                      binding.descriptor_indices)) return false;
    }
    const float y = 720.f - draw.viewport[3];
    binding.viewport = {0, y, 1280, draw.viewport[3],
                        draw.viewport[4], draw.viewport[5]};
    binding.scissor = {draw.scissor[0], LONG(draw.scissor[1] + y),
                       draw.scissor[2], LONG(draw.scissor[3] + y)};
    bindings.push_back(binding);
  }
  return !bindings.empty();
}
}  // namespace

bool DrawNativeOutputUi(
    const rex::system::NativeGuestOutputRenderContext& context,
    uint64_t source_frame) {
  static thread_local Graphics graphics;
  auto* device = static_cast<ID3D12Device*>(context.device);
  auto* output = static_cast<ID3D12Resource*>(context.guest_output);
  auto* list = static_cast<rex::graphics::d3d12::DeferredCommandList*>(
      context.deferred_command_list);
  if (!device || !output || !list ||
      output->GetDesc().Format != DXGI_FORMAT_R10G10B10A2_UNORM ||
      output->GetDesc().Width != 1280 || output->GetDesc().Height < 720 ||
      !graphics.Ready(context)) return false;
  while (!graphics.submitted.empty() &&
         graphics.submitted.front().first <= context.completed_submission)
    graphics.submitted.pop_front();
  if (graphics.submitted.size() >= 8) return false;

  Upload upload;
  std::vector<Binding> bindings;
  size_t skipped = 0;
  if (!WithOrderedUiFrame(source_frame, [&](const auto& draws) {
        return Prepare(draws, upload, bindings, skipped);
      })) {
    REXGPU_WARN("FH1 UI pilot preparation rejected source_frame={}", source_frame);
    return false;
  }
  Frame frame;
  size_t textured = 0;
  for (auto& binding : bindings)
    if (binding.kind) { binding.view_offset = uint32_t(textured * 4); ++textured; }
  if (textured) {
    if (!context.texture) return false;
    D3D12_DESCRIPTOR_HEAP_DESC views_desc{};
    views_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    views_desc.NumDescriptors = uint32_t(textured * 4);
    views_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    D3D12_DESCRIPTOR_HEAP_DESC samplers_desc{};
    samplers_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplers_desc.NumDescriptors = 2;
    samplers_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&views_desc,
                                           IID_PPV_ARGS(&frame.views))) ||
        FAILED(device->CreateDescriptorHeap(&samplers_desc,
                                           IID_PPV_ARGS(&frame.samplers))))
      return false;
    D3D12_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW =
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    const auto sampler_step = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    auto sampler_cpu = frame.samplers->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < 2; ++i, sampler_cpu.ptr += sampler_step)
      device->CreateSampler(&sampler, sampler_cpu);

    const auto view_step = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto view_start = frame.views->GetCPUDescriptorHandleForHeapStart();
    for (const auto& binding : bindings) {
      if (!binding.kind) continue;
      for (uint32_t fetch = 0; fetch < binding.kind; ++fetch) {
        const auto& identity = binding.textures[fetch];
        void* borrowed = nullptr;
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        bool immutable = false;
        if (!context.texture(context, identity.fetch_words,
                             identity.allocation_id,
                             identity.payload_generation, &borrowed,
                             &view, &immutable) || !borrowed || !immutable ||
            view.ViewDimension != D3D12_SRV_DIMENSION_TEXTURE2D) {
          REXGPU_WARN("FH1 UI pilot texture unavailable source_frame={} "
                      "allocation={} generation={}", source_frame,
                      identity.allocation_id, identity.payload_generation);
          return false;
        }
        auto* resource = static_cast<ID3D12Resource*>(borrowed);
        frame.textures.emplace_back(resource);
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        view.Texture2DArray.MostDetailedMip = 0;
        view.Texture2DArray.MipLevels = 1;
        view.Texture2DArray.FirstArraySlice = 0;
        view.Texture2DArray.ArraySize = 1;
        const uint32_t index = binding.view_offset + fetch * 2;
        for (uint32_t sign = 0; sign < 2; ++sign) {
          D3D12_CPU_DESCRIPTOR_HANDLE cpu{
              view_start.ptr + SIZE_T(index + sign) * view_step};
          device->CreateShaderResourceView(resource, &view, cpu);
        }
      }
    }
  }
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC buffer{};
  buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer.Width = upload.bytes.size();
  buffer.Height = 1;
  buffer.DepthOrArraySize = 1;
  buffer.MipLevels = 1;
  buffer.SampleDesc.Count = 1;
  buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (FAILED(device->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_NONE, &buffer,
          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
          IID_PPV_ARGS(&frame.upload)))) return false;
  void* mapped = nullptr;
  if (FAILED(frame.upload->Map(0, nullptr, &mapped))) return false;
  std::memcpy(mapped, upload.bytes.data(), upload.bytes.size());
  frame.upload->Unmap(0, nullptr);
  D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
  rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_desc.NumDescriptors = 1;
  if (FAILED(device->CreateDescriptorHeap(&rtv_desc,
                                          IID_PPV_ARGS(&frame.rtv))))
    return false;
  const auto rtv = frame.rtv->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(output, nullptr, rtv);
  const auto base = frame.upload->GetGPUVirtualAddress();
  const auto view_gpu = frame.views
      ? frame.views->GetGPUDescriptorHandleForHeapStart()
      : D3D12_GPU_DESCRIPTOR_HANDLE{};
  const auto sampler_gpu = frame.samplers
      ? frame.samplers->GetGPUDescriptorHandleForHeapStart()
      : D3D12_GPU_DESCRIPTOR_HANDLE{};
  ID3D12DescriptorHeap* view_heap = frame.views.Get();
  ID3D12DescriptorHeap* sampler_heap = frame.samplers.Get();
  graphics.submitted.emplace_back(context.submission, std::move(frame));
  list->ReserveAdditionalBytes(bindings.size() * 256 + 2048);
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = output;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore =
      D3D12_RESOURCE_STATES(context.guest_output_state);
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
  list->D3DResourceBarrier(1, &barrier);
  list->D3DOMSetRenderTargets(1, &rtv, FALSE, nullptr);
  if (textured) list->SetDescriptorHeaps(view_heap, sampler_heap);
  list->D3DSetGraphicsRootSignature(graphics.root.Get());
  list->D3DIASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  list->D3DSetGraphicsRootUnorderedAccessView(
      5, graphics.dummy_uav->GetGPUVirtualAddress());
  for (const auto& binding : bindings) {
    list->D3DSetPipelineState(graphics.pipelines[binding.kind].Get());
    list->RSSetViewport(binding.viewport);
    list->RSSetScissorRect(binding.scissor);
    D3D12_INDEX_BUFFER_VIEW index{
        base + binding.index, binding.count * 2, DXGI_FORMAT_R16_UINT};
    list->D3DIASetIndexBuffer(&index);
    list->D3DSetGraphicsRootConstantBufferView(0, base + binding.system);
    list->D3DSetGraphicsRootConstantBufferView(
        1, base + binding.vertex_constants);
    list->D3DSetGraphicsRootConstantBufferView(
        2, base + binding.pixel_constants);
    list->D3DSetGraphicsRootConstantBufferView(3, base + binding.fetch);
    list->D3DSetGraphicsRootShaderResourceView(4, base + binding.vertex);
    if (binding.kind) {
      list->D3DSetGraphicsRootConstantBufferView(6, base + binding.fetch);
      list->D3DSetGraphicsRootConstantBufferView(
          7, base + binding.descriptor_indices);
      list->D3DSetGraphicsRootDescriptorTable(8, sampler_gpu);
      D3D12_GPU_DESCRIPTOR_HANDLE view = view_gpu;
      view.ptr += SIZE_T(binding.view_offset) *
          device->GetDescriptorHandleIncrementSize(
              D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      list->D3DSetGraphicsRootDescriptorTable(9, view);
    }
    list->D3DDrawIndexedInstanced(binding.count, 1, 0, 0, 0);
  }
  std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
  list->D3DResourceBarrier(1, &barrier);
  REXGPU_INFO("FH1 UI pilot source_frame={} draws={} skipped={}",
              source_frame, bindings.size(), skipped);
  return true;
}

}  // namespace pinyon_shift::native_renderer
