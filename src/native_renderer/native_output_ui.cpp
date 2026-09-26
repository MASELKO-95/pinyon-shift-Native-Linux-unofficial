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
#include <vector>

#include <rex/graphics/d3d12/deferred_command_list.h>
#include <rex/logging.h>
#include <rex/system/interfaces/graphics.h>

#include "native_renderer/ordered_ui_capture.h"

namespace pinyon_shift::native_renderer {
namespace {
using Microsoft::WRL::ComPtr;

constexpr uint64_t kVertexShader = 0xED90DA6EFF5C6BCAull;
constexpr uint64_t kPixelShader = 0x57B9400F6B398736ull;
constexpr uint64_t kSpecialization = 3;
constexpr size_t kMaxUpload = 128 * 1024 * 1024;

struct Binding {
  uint64_t index, vertex, system, vertex_constants, pixel_constants, fetch;
  uint32_t count;
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
  ComPtr<ID3D12DescriptorHeap> rtv;
};

struct Graphics {
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12RootSignature> root;
  ComPtr<ID3D12PipelineState> pipeline;
  ComPtr<ID3D12Resource> dummy_uav;
  std::deque<std::pair<uint64_t, Frame>> submitted;

  bool Ready(const rex::system::NativeGuestOutputRenderContext& context) {
    auto* current = static_cast<ID3D12Device*>(context.device);
    if (device.Get() != current) {
      submitted.clear();
      root.Reset();
      pipeline.Reset();
      dummy_uav.Reset();
      device = current;
    }
    if (pipeline) return true;
    if (!device || !context.shader) return false;
    const uint8_t *vertex = nullptr, *pixel = nullptr;
    size_t vertex_size = 0, pixel_size = 0;
    if (!context.shader(context, 0, kVertexShader, kSpecialization,
                        &vertex, &vertex_size) ||
        !context.shader(context, 1, kPixelShader, kSpecialization,
                        &pixel, &pixel_size) ||
        !vertex || !pixel || vertex_size < 4 || pixel_size < 4 ||
        std::memcmp(vertex, "DXBC", 4) || std::memcmp(pixel, "DXBC", 4))
      return false;

    D3D12_ROOT_PARAMETER parameters[6]{};
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
    D3D12_ROOT_SIGNATURE_DESC root_desc{
        6, parameters, 0, nullptr,
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
    desc.VS = {vertex, vertex_size};
    desc.PS = {pixel, pixel_size};
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
    const HRESULT result = device->CreateGraphicsPipelineState(
        &desc, IID_PPV_ARGS(&pipeline));
    if (FAILED(result)) {
      REXGPU_INFO("FH1 UI pilot pipeline rejected hresult={:08X}",
                  uint32_t(result));
      return false;
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
      pipeline.Reset();
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
    if (draw.vertex_shader != kVertexShader ||
        draw.pixel_shader != kPixelShader ||
        draw.vertex_specialization != kSpecialization ||
        draw.pixel_specialization != kSpecialization) {
      ++skipped;
      continue;
    }
    if (draw.vertices.size() != 1 || !draw.texture_fetches.empty() ||
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
      })) return false;

  Frame frame;
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
  list->D3DSetGraphicsRootSignature(graphics.root.Get());
  list->D3DSetPipelineState(graphics.pipeline.Get());
  list->D3DIASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  list->D3DSetGraphicsRootUnorderedAccessView(
      5, graphics.dummy_uav->GetGPUVirtualAddress());
  for (const auto& binding : bindings) {
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
    list->D3DDrawIndexedInstanced(binding.count, 1, 0, 0, 0);
  }
  std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
  list->D3DResourceBarrier(1, &barrier);
  REXGPU_INFO("FH1 UI pilot source_frame={} draws={} skipped={}",
              source_frame, bindings.size(), skipped);
  return true;
}

}  // namespace pinyon_shift::native_renderer
