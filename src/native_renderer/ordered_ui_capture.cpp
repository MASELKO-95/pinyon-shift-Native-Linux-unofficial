#include "native_renderer/ordered_ui_capture.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <rex/logging.h>
#include <rex/system/interfaces/graphics.h>

#include "pinyon_shift_diagnostics.h"

namespace pinyon_shift::native_renderer {
namespace {

constexpr uint64_t kMaximumPayloadBytes = 128ull * 1024 * 1024;
constexpr size_t kMaximumDraws = 256;
static_assert(sizeof(rex::system::GraphicsPreparedDrawTextureFetch) == 36);
static_assert(sizeof(rex::system::GraphicsFinalDrawTextureIdentity) == 56);

struct Vertex {
  uint32_t constant = 0, stride = 0, base = 0, length = 0, type = 0;
  uint64_t hash = 0;
  std::vector<uint8_t> bytes;
};

struct Draw {
  uint64_t sequence = 0, vertex_shader = 0, pixel_shader = 0;
  uint64_t vertex_specialization = 0, pixel_specialization = 0;
  uint32_t primitive = 0, index_type = 0, index_count = 0;
  uint32_t index_base = 0, index_length = 0, index_endianness = 0;
  uint32_t surface = 0, color = 0, depth = 0, target_bits = 0;
  uint32_t depth_control = 0, color_mask = 0;
  uint64_t index_hash = 0;
  std::vector<uint8_t> indices;
  std::vector<Vertex> vertices;
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

struct Frame {
  uint64_t source_frame = 0, payload_bytes = 0;
  bool rejected = false;
  std::map<uint64_t, Draw> draws;
};

std::mutex capture_mutex;
Frame captured;

void Reject(uint64_t frame) {
  std::lock_guard lock(capture_mutex);
  if (captured.source_frame != frame) captured = {};
  captured.source_frame = frame;
  captured.rejected = true;
}

template <typename T>
void Write(std::ofstream& stream, const T& value) {
  stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

template <typename T, size_t N>
void Write(std::ofstream& stream, const std::array<T, N>& value) {
  stream.write(reinterpret_cast<const char*>(value.data()), sizeof(value));
}

void WriteBytes(std::ofstream& stream, const std::vector<uint8_t>& bytes) {
  if (!bytes.empty())
    stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

}  // namespace

void CaptureOrderedUiDraw(
    const rex::system::GraphicsPreparedDrawObservation& observation) {
  if (!observation.draw_sequence || !observation.vertex_float_constant_words ||
      !observation.vertex_float_constant_bitmap ||
      !observation.pixel_float_constant_bitmap ||
      observation.vertex_fetch_count > observation.vertex_fetch_capacity ||
      observation.vertex_fetch_count > 8 ||
      (observation.vertex_fetch_count && !observation.vertex_fetches) ||
      observation.texture_fetch_count > 32 ||
      (observation.texture_fetch_count && !observation.texture_fetches)) {
    Reject(observation.frame_sequence);
    return;
  }

  Draw draw;
  draw.sequence = observation.draw_sequence;
  draw.vertex_shader = observation.vertex_shader_hash;
  draw.pixel_shader = observation.pixel_shader_hash;
  draw.vertex_specialization = observation.vertex_specialization_mask;
  draw.pixel_specialization = observation.pixel_specialization_mask;
  draw.primitive = observation.guest_primitive_type;
  draw.index_type = observation.index_buffer_type;
  draw.index_count = observation.index_count;
  draw.index_base = observation.index_buffer_guest_base;
  draw.index_length = observation.index_buffer_length;
  draw.index_endianness = observation.index_buffer_guest_endianness;
  draw.surface = observation.surface_info;
  draw.color = observation.color_info[0];
  draw.depth = observation.depth_info;
  draw.target_bits = observation.bound_render_target_bits;
  draw.depth_control = observation.normalized_depth_control;
  draw.color_mask = observation.normalized_color_mask;
  std::copy_n(observation.vertex_float_constant_words, draw.constants.size(),
              draw.constants.begin());
  std::copy_n(observation.vertex_float_constant_bitmap, 4,
              draw.vertex_bitmap.begin());
  std::copy_n(observation.pixel_float_constant_bitmap, 4,
              draw.pixel_bitmap.begin());

  uint64_t payload_bytes = sizeof(draw.constants);
  for (uint32_t i = 0; i < observation.vertex_fetch_count; ++i) {
    const auto& input = observation.vertex_fetches[i];
    if (input.cpu_snapshot_status != 1 || !input.cpu_snapshot_bytes) {
      Reject(observation.frame_sequence);
      return;
    }
    Vertex vertex;
    vertex.constant = input.fetch_constant;
    vertex.stride = input.stride_words;
    vertex.base = input.guest_base;
    vertex.length = input.length;
    vertex.type = input.type;
    vertex.hash = input.cpu_snapshot_hash;
    vertex.bytes.assign(input.cpu_snapshot_bytes,
                        input.cpu_snapshot_bytes + input.length);
    payload_bytes += vertex.bytes.size();
    draw.vertices.push_back(std::move(vertex));
  }
  if (draw.index_type) {
    if (observation.index_cpu_snapshot_status != 1 ||
        !observation.index_cpu_snapshot_bytes) {
      Reject(observation.frame_sequence);
      return;
    }
    draw.index_hash = observation.index_cpu_snapshot_hash;
    draw.indices.assign(observation.index_cpu_snapshot_bytes,
                        observation.index_cpu_snapshot_bytes + draw.index_length);
    payload_bytes += draw.indices.size();
  }
  if (observation.texture_fetch_count)
    draw.texture_fetches.assign(observation.texture_fetches,
                                observation.texture_fetches +
                                    observation.texture_fetch_count);

  std::lock_guard lock(capture_mutex);
  if (captured.source_frame &&
      captured.source_frame != observation.frame_sequence)
    captured = {};
  captured.source_frame = observation.frame_sequence;
  if (captured.draws.size() >= kMaximumDraws ||
      payload_bytes > kMaximumPayloadBytes -
                          std::min(captured.payload_bytes, kMaximumPayloadBytes) ||
      !captured.draws.emplace(draw.sequence, std::move(draw)).second) {
    captured.rejected = true;
    return;
  }
  captured.payload_bytes += payload_bytes;
}

void CaptureOrderedUiFinalState(
    const rex::system::GraphicsFinalDrawStateObservation& observation) {
  std::lock_guard lock(capture_mutex);
  if (captured.source_frame != observation.frame_sequence) return;
  const auto found = captured.draws.find(observation.draw_sequence);
  if (found == captured.draws.end()) return;
  auto& draw = found->second;
  if (draw.final_seen || !observation.system_constant_words ||
      observation.system_constant_word_count < draw.system.size() ||
      !observation.fetch_constant_words ||
      observation.fetch_constant_word_count < draw.fetch_constants.size() ||
      !observation.viewport || !observation.scissor ||
      observation.texture_count > 32 ||
      (observation.texture_count && !observation.textures)) {
    captured.rejected = true;
    return;
  }
  draw.final_seen = true;
  draw.raster = observation.raster_mode_control;
  draw.clip = observation.clip_control;
  draw.final_depth = observation.normalized_depth_control;
  std::copy_n(observation.system_constant_words, draw.system.size(),
              draw.system.begin());
  std::copy_n(observation.fetch_constant_words, draw.fetch_constants.size(),
              draw.fetch_constants.begin());
  std::copy_n(observation.viewport, draw.viewport.size(), draw.viewport.begin());
  std::copy_n(observation.scissor, draw.scissor.size(), draw.scissor.begin());
  if (observation.texture_count)
    draw.textures.assign(observation.textures,
                         observation.textures + observation.texture_count);
}

void FlushOrderedUiFrame(uint64_t output_frame) {
  Frame frame;
  {
    std::lock_guard lock(capture_mutex);
    if (!captured.source_frame || captured.source_frame != output_frame + 1)
      return;
    frame = std::move(captured);
    captured = {};
  }
  const bool complete = !frame.rejected && !frame.draws.empty() &&
      std::all_of(frame.draws.begin(), frame.draws.end(),
                  [](const auto& entry) { return entry.second.final_seen; }) &&
      frame.draws.rbegin()->first - frame.draws.begin()->first + 1 ==
          frame.draws.size();
  const auto output = diagnostics::EnvironmentPath(
      "PINYON_SHIFT_FH1_RENDER_TEST_OUTPUT");
  if (!output) return;
  std::error_code error;
  std::filesystem::create_directories(*output, error);
  if (error) return;
  const auto path = *output /
      ("ordered-ui-" + std::to_string(frame.source_frame) + ".bin");
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) return;
  stream.write("RAYUI001", 8);
  Write(stream, frame.source_frame);
  Write(stream, uint32_t(frame.draws.size()));
  Write(stream, uint32_t(complete));
  for (const auto& [sequence, draw] : frame.draws) {
    Write(stream, sequence);
    Write(stream, draw.vertex_shader);
    Write(stream, draw.pixel_shader);
    Write(stream, draw.vertex_specialization);
    Write(stream, draw.pixel_specialization);
    for (uint32_t value : {draw.primitive, draw.index_type, draw.index_count,
                           draw.index_base, draw.index_length,
                           draw.index_endianness, draw.surface, draw.color,
                           draw.depth, draw.target_bits, draw.depth_control,
                           draw.color_mask})
      Write(stream, value);
    Write(stream, draw.constants);
    Write(stream, draw.vertex_bitmap);
    Write(stream, draw.pixel_bitmap);
    Write(stream, uint32_t(draw.vertices.size()));
    for (const auto& vertex : draw.vertices) {
      for (uint32_t value : {vertex.constant, vertex.stride, vertex.base,
                             vertex.length, vertex.type})
        Write(stream, value);
      Write(stream, vertex.hash);
      WriteBytes(stream, vertex.bytes);
    }
    Write(stream, draw.index_hash);
    WriteBytes(stream, draw.indices);
    Write(stream, uint32_t(draw.texture_fetches.size()));
    for (const auto& texture : draw.texture_fetches)
      Write(stream, texture);
    Write(stream, uint32_t(draw.final_seen));
    Write(stream, draw.raster);
    Write(stream, draw.clip);
    Write(stream, draw.final_depth);
    Write(stream, draw.system);
    Write(stream, draw.fetch_constants);
    Write(stream, draw.viewport);
    Write(stream, draw.scissor);
    Write(stream, uint32_t(draw.textures.size()));
    for (const auto& texture : draw.textures)
      Write(stream, texture);
  }
  stream.close();
  REXGPU_INFO("FH1 RAY00 UI frame source={} draws={} payload_bytes={} "
              "complete={} written={} path={}", frame.source_frame,
              frame.draws.size(), frame.payload_bytes, complete, bool(stream),
              path.string());
}

}  // namespace pinyon_shift::native_renderer
