#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include <rex/system/interfaces/graphics.h>

namespace rex::system {
struct GraphicsPreparedDrawObservation;
struct GraphicsFinalDrawStateObservation;
struct GraphicsCopyObservation;
struct GraphicsFh1ClearObservation;
}

namespace pinyon_shift::native_renderer {

struct OrderedUiVertex {
  uint32_t constant = 0, stride = 0, base = 0, length = 0, type = 0;
  uint64_t hash = 0;
  std::vector<uint8_t> bytes;
};

struct OrderedUiDraw {
  uint64_t sequence = 0, vertex_shader = 0, pixel_shader = 0;
  uint64_t vertex_specialization = 0, pixel_specialization = 0;
  uint32_t primitive = 0, index_type = 0, index_count = 0;
  uint32_t index_base = 0, index_length = 0, index_endianness = 0;
  uint32_t surface = 0, color = 0, depth = 0, target_bits = 0;
  uint32_t depth_control = 0, color_mask = 0;
  uint64_t index_hash = 0;
  std::vector<uint8_t> indices;
  std::vector<OrderedUiVertex> vertices;
  std::array<uint32_t, 2048> constants{};
  std::array<uint64_t, 4> vertex_bitmap{}, pixel_bitmap{};
  std::vector<rex::system::GraphicsPreparedDrawTextureFetch> texture_fetches;
  bool final_seen = false;
  uint32_t raster = 0, clip = 0, final_depth = 0;
  std::array<uint32_t, 64> system{};
  std::array<uint32_t, 192> fetch_constants{};
  std::array<float, 6> viewport{};
  std::array<int32_t, 4> scissor{};
  std::vector<rex::system::GraphicsFinalDrawTextureIdentity> textures;
};

bool WithOrderedUiFrame(
    uint64_t source_frame,
    const std::function<bool(const std::map<uint64_t, OrderedUiDraw>&)>& use);
uint64_t ResolveOrderedUiReplayFrame(uint64_t source_frame);

void CaptureOrderedUiDraw(
    const rex::system::GraphicsPreparedDrawObservation& observation);
void CaptureOrderedFrameDraw(
    const rex::system::GraphicsPreparedDrawObservation& observation);
void CaptureOrderedFrameCopy(
    const rex::system::GraphicsCopyObservation& observation);
void CaptureOrderedFrameClear(
    const rex::system::GraphicsFh1ClearObservation& observation);
void CaptureOrderedUiFinalState(
    const rex::system::GraphicsFinalDrawStateObservation& observation);
void FlushOrderedUiFrame(uint64_t output_frame);

}  // namespace pinyon_shift::native_renderer
